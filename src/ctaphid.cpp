#include "ctaphid.h"
#include "USB.h"
#include "USBHID.h"

CtapHid ctapHid;

// ─── FIDO CTAPHID USB Report Descriptor ──────────────────────────────────────
// Usage Page (0xF1D0), Usage (0x01 - Authenticator), Report Size (64 in / 64 out)
const uint8_t fido_hid_report_descriptor[] = {
    0x06, 0xD0, 0xF1, // Usage Page (FIDO Alliance 0xF1D0)
    0x09, 0x01,       // Usage (U2F / FIDO Authenticator 0x01)
    0xA1, 0x01,       // Collection (Application)
    
    // Raw IN report (64 bytes)
    0x09, 0x20,       // Usage (Data In)
    0x15, 0x00,       // Logical Minimum (0)
    0x26, 0xFF, 0x00, // Logical Maximum (255)
    0x75, 0x08,       // Report Size (8 bits)
    0x95, 0x40,       // Report Count (64 bytes)
    0x81, 0x02,       // Input (Data, Variable, Absolute)

    // Raw OUT report (64 bytes)
    0x09, 0x21,       // Usage (Data Out)
    0x15, 0x00,       // Logical Minimum (0)
    0x26, 0xFF, 0x00, // Logical Maximum (255)
    0x75, 0x08,       // Report Size (8 bits)
    0x95, 0x40,       // Report Count (64 bytes)
    0x91, 0x02,       // Output (Data, Variable, Absolute)

    0xC0              // End Collection
};

const size_t fido_hid_report_descriptor_len = sizeof(fido_hid_report_descriptor);

// Static custom USB HID Device
static USBHID HID;

#define CTAPHID_QUEUE_DEPTH 8
static uint8_t s_rxQueue[CTAPHID_QUEUE_DEPTH][CTAPHID_PACKET_SIZE];
static volatile uint8_t s_rxHead = 0;
static volatile uint8_t s_rxTail = 0;

class FidoHidDevice : public USBHIDDevice {
public:
    FidoHidDevice() {
        static bool initialized = false;
        if (!initialized) {
            initialized = true;
            HID.addDevice(this, fido_hid_report_descriptor_len);
        }
    }

    void begin() {
        HID.begin();
    }

    uint16_t _onGetDescriptor(uint8_t* dst) override {
        memcpy(dst, fido_hid_report_descriptor, fido_hid_report_descriptor_len);
        return fido_hid_report_descriptor_len;
    }

    void _onOutput(uint8_t report_id, const uint8_t* buffer, uint16_t len) override {
        if (!buffer || len == 0) return;
        uint8_t nextHead = (s_rxHead + 1) % CTAPHID_QUEUE_DEPTH;
        if (nextHead != s_rxTail) { // queue not full
            if (len == 63) {
                s_rxQueue[s_rxHead][0] = 0x00;
                memcpy(s_rxQueue[s_rxHead] + 1, buffer, 63);
            } else {
                memcpy(s_rxQueue[s_rxHead], buffer, (len > 64) ? 64 : len);
            }
            s_rxHead = nextHead;
        }
    }

    bool sendReport(const uint8_t* report) {
        uint32_t start = millis();
        while (millis() - start < 500) {
            if (HID.ready()) {
                if (HID.SendReport(0, report, CTAPHID_PACKET_SIZE)) {
                    return true;
                }
            }
            delay(1);
        }
        return false;
    }
};

static FidoHidDevice fidoDev;

CtapHid::CtapHid() 
    : _nextCid(1), _isReceiving(false), _lastPacketTime(0), _cborHandler(nullptr), _msgHandler(nullptr), _winkHandler(nullptr), _diagnosticMode(false) {
    memset(&_rxMsg, 0, sizeof(_rxMsg));
}

void CtapHid::begin(bool diagnosticMode) {
    _diagnosticMode = diagnosticMode;

    fidoDev.begin();
    
    // Start USB
    USB.VID(0x303A); // Espressif
    USB.PID(0x1001); // Standard Espressif Composite CDC + HID PID
    USB.productName("Crypto TKey S3 Authenticator");
    USB.manufacturerName("Reaper Security Systems");
    USB.serialNumber("TKEY-S3-007");
    USB.begin();
}

uint32_t CtapHid::allocateCid() {
    uint32_t cid = _nextCid++;
    if (_nextCid == 0 || _nextCid == CTAPHID_BROADCAST_CID) {
        _nextCid = 1;
    }
    return cid;
}

void CtapHid::process() {
    // 1. Drain incoming USB HID packet queue on main application thread
    while (s_rxTail != s_rxHead) {
        uint8_t curTail = s_rxTail;
        handleIncomingPacket(s_rxQueue[curTail], CTAPHID_PACKET_SIZE);
        s_rxTail = (curTail + 1) % CTAPHID_QUEUE_DEPTH;
    }

    // 2. Timeout watchdog for fragmented packets
    if (_isReceiving && (millis() - _lastPacketTime > 3000)) {
        sendError(_rxMsg.cid, CTAP1_ERR_TIMEOUT);
        _isReceiving = false;
        memset(&_rxMsg, 0, sizeof(_rxMsg));
    }
}

void CtapHid::handleIncomingPacket(const uint8_t* buffer, uint16_t len) {
    if (!buffer || len < CTAPHID_PACKET_SIZE) return;

    uint32_t cid = ((uint32_t)buffer[0] << 24) |
                   ((uint32_t)buffer[1] << 16) |
                   ((uint32_t)buffer[2] << 8)  |
                   ((uint32_t)buffer[3]);

    if (buffer[4] & 0x80) {
        // Initialization packet
        uint8_t cmd = buffer[4] & 0x7F;
        uint16_t totalLen = ((uint16_t)buffer[5] << 8) | buffer[6];
        Serial.printf("[CTAPHID IN] cid=0x%08X rawCmd=0x%02X len=%u\n", cid, buffer[4], totalLen);

        if (totalLen > CTAPHID_MAX_MSG_LEN) {
            sendError(cid, CTAP1_ERR_INVALID_LENGTH);
            _isReceiving = false;
            return;
        }

        _rxMsg.cid = cid;
        _rxMsg.cmd = cmd;
        _rxMsg.length = totalLen;
        _rxMsg.seq = 0;
        _rxMsg.offset = 0;

        uint16_t chunk = (totalLen > 57) ? 57 : totalLen;
        memcpy(_rxMsg.data, buffer + 7, chunk);
        _rxMsg.offset = chunk;
        _isReceiving = (_rxMsg.offset < _rxMsg.length);
        _lastPacketTime = millis();

        if (!_isReceiving) {
            Serial.printf("[CTAPHID DISPATCH] cmd=0x%02X (hasCborHandler=%d)\n", _rxMsg.cmd, _cborHandler != nullptr);
            dispatchMessage();
        }
    } else {
        // Continuation packet
        if (!_isReceiving || cid != _rxMsg.cid) {
            return;
        }

        uint8_t seq = buffer[4];
        if (seq != _rxMsg.seq) {
            sendError(cid, CTAP1_ERR_INVALID_SEQ);
            _isReceiving = false;
            return;
        }

        _rxMsg.seq++;
        uint16_t rem = _rxMsg.length - _rxMsg.offset;
        uint16_t chunk = (rem > 59) ? 59 : rem;
        memcpy(_rxMsg.data + _rxMsg.offset, buffer + 5, chunk);
        _rxMsg.offset += chunk;
        _lastPacketTime = millis();

        if (_rxMsg.offset >= _rxMsg.length) {
            _isReceiving = false;
            dispatchMessage();
        }
    }
}

void CtapHid::dispatchMessage() {
    switch (_rxMsg.cmd) {
        case CTAPHID_CMD_INIT:
            handleInit(_rxMsg.cid, _rxMsg.data, _rxMsg.length);
            break;
        case CTAPHID_CMD_PING:
            handlePing(_rxMsg.cid, _rxMsg.data, _rxMsg.length);
            break;
        case CTAPHID_CMD_MSG:
            if (_msgHandler) {
                _msgHandler(_rxMsg.cid, _rxMsg.data, _rxMsg.length);
            } else {
                sendError(_rxMsg.cid, CTAP1_ERR_INVALID_COMMAND);
            }
            break;
        case CTAPHID_CMD_WINK:
            handleWink(_rxMsg.cid);
            break;
        case CTAPHID_CMD_CBOR:
            if (_cborHandler) {
                _cborHandler(_rxMsg.cid, _rxMsg.data, _rxMsg.length);
            } else {
                sendError(_rxMsg.cid, CTAP1_ERR_INVALID_COMMAND);
            }
            break;
        case CTAPHID_CMD_CANCEL:
            // Cancel pending operation
            break;
        default:
            sendError(_rxMsg.cid, CTAP1_ERR_INVALID_COMMAND);
            break;
    }
}

void CtapHid::handleInit(uint32_t cid, const uint8_t* payload, uint16_t len) {
    if (len < 8) {
        sendError(cid, CTAP1_ERR_INVALID_LENGTH);
        return;
    }

    const uint8_t* nonce = payload;
    uint32_t allocatedCid = (cid == CTAPHID_BROADCAST_CID) ? allocateCid() : cid;

    // INIT Response packet:
    // Nonce (8 bytes) || Allocated CID (4 bytes) || Protocol Version (1) || Major (1) || Minor (1) || Build (1) || Capabilities (1)
    uint8_t resp[17];
    memcpy(resp, nonce, 8);
    resp[8]  = (uint8_t)(allocatedCid >> 24);
    resp[9]  = (uint8_t)(allocatedCid >> 16);
    resp[10] = (uint8_t)(allocatedCid >> 8);
    resp[11] = (uint8_t)allocatedCid;
    resp[12] = 0x02; // Protocol version CTAPHID 2.0
    resp[13] = 0x01; // Major version
    resp[14] = 0x00; // Minor version
    resp[15] = 0x00; // Build version
    resp[16] = 0x05; // Capabilities: CAPFLAG_WINK (0x01) | CAPFLAG_CBOR (0x04)

    sendResponse(cid, CTAPHID_CMD_INIT, resp, sizeof(resp));
}

void CtapHid::handlePing(uint32_t cid, const uint8_t* payload, uint16_t len) {
    sendResponse(cid, CTAPHID_CMD_PING, payload, len);
}

void CtapHid::handleWink(uint32_t cid) {
    if (_winkHandler) {
        _winkHandler(cid);
    }
    sendResponse(cid, CTAPHID_CMD_WINK, nullptr, 0);
}

bool CtapHid::sendResponse(uint32_t cid, uint8_t cmd, const uint8_t* payload, uint16_t len) {
    uint8_t packet[CTAPHID_PACKET_SIZE];
    uint16_t bytesRemaining = len;
    uint16_t payloadOffset = 0;
    uint8_t seq = 0;

    // 1. Initialization Packet
    memset(packet, 0, CTAPHID_PACKET_SIZE);
    packet[0] = (uint8_t)(cid >> 24);
    packet[1] = (uint8_t)(cid >> 16);
    packet[2] = (uint8_t)(cid >> 8);
    packet[3] = (uint8_t)cid;
    packet[4] = cmd | 0x80;
    packet[5] = (uint8_t)(len >> 8);
    packet[6] = (uint8_t)len;

    uint16_t chunkLen = (bytesRemaining > 57) ? 57 : bytesRemaining;
    if (payload && chunkLen > 0) {
        memcpy(packet + 7, payload, chunkLen);
    }
    bytesRemaining -= chunkLen;
    payloadOffset += chunkLen;

    if (!fidoDev.sendReport(packet)) return false;

    // 2. Continuation Packets (if payload > 57 bytes)
    while (bytesRemaining > 0) {
        delay(1);
        memset(packet, 0, CTAPHID_PACKET_SIZE);
        packet[0] = (uint8_t)(cid >> 24);
        packet[1] = (uint8_t)(cid >> 16);
        packet[2] = (uint8_t)(cid >> 8);
        packet[3] = (uint8_t)cid;
        packet[4] = seq++ & 0x7F;

        chunkLen = (bytesRemaining > 59) ? 59 : bytesRemaining;
        if (payload && chunkLen > 0) {
            memcpy(packet + 5, payload + payloadOffset, chunkLen);
        }
        bytesRemaining -= chunkLen;
        payloadOffset += chunkLen;

        if (!fidoDev.sendReport(packet)) return false;
    }

    return true;
}

bool CtapHid::sendError(uint32_t cid, uint8_t errorCode) {
    return sendResponse(cid, CTAPHID_CMD_ERROR, &errorCode, 1);
}

bool CtapHid::sendKeepAlive(uint32_t cid, uint8_t status) {
    return sendResponse(cid, CTAPHID_CMD_KEEPALIVE, &status, 1);
}
