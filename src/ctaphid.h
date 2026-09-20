#pragma once
#include <Arduino.h>

/**
 * CTAPHID Protocol Definitions & Command Codes
 * Standard: FIDO Alliance FIDO2 / CTAP2 Specification
 */

#define CTAPHID_CMD_PING        0x01
#define CTAPHID_CMD_MSG         0x03
#define CTAPHID_CMD_LOCK        0x04
#define CTAPHID_CMD_INIT        0x06
#define CTAPHID_CMD_WINK        0x08
#define CTAPHID_CMD_CBOR        0x10
#define CTAPHID_CMD_CANCEL      0x11
#define CTAPHID_CMD_ERROR       0x3F
#define CTAPHID_CMD_KEEPALIVE   0x3B

// Status & Error Codes
#define CTAP1_ERR_SUCCESS             0x00
#define CTAP1_ERR_INVALID_COMMAND     0x01
#define CTAP1_ERR_INVALID_PARAMETER   0x02
#define CTAP1_ERR_INVALID_LENGTH      0x03
#define CTAP1_ERR_INVALID_SEQ         0x04
#define CTAP1_ERR_TIMEOUT             0x05
#define CTAP1_ERR_CHANNEL_BUSY        0x06
#define CTAP1_ERR_LOCK_REQUIRED       0x0A
#define CTAP1_ERR_INVALID_CHANNEL     0x0B
#define CTAP1_ERR_OTHER               0x7F

#define CTAP2_ERR_USER_ACTION_TIMEOUT 0x27
#define CTAP2_ERR_UP_REQUIRED         0x2E

#define CTAPHID_BROADCAST_CID   0xFFFFFFFF
#define CTAPHID_PACKET_SIZE     64
#define CTAPHID_INIT_HEADER_LEN 7
#define CTAPHID_CONT_HEADER_LEN 5
#define CTAPHID_MAX_MSG_LEN     1024

// FIDO Usage Page HID Report Descriptor
extern const uint8_t fido_hid_report_descriptor[];
extern const size_t fido_hid_report_descriptor_len;

struct CtapMessage {
    uint32_t cid;
    uint8_t  cmd;
    uint16_t length;
    uint16_t offset;
    uint8_t  seq;
    uint8_t  data[CTAPHID_MAX_MSG_LEN];
};

class CtapHid {
public:
    CtapHid();
    void begin(bool diagnosticMode = false);
    void process();
    bool sendResponse(uint32_t cid, uint8_t cmd, const uint8_t* payload, uint16_t len);
    bool sendError(uint32_t cid, uint8_t errorCode);
    bool sendKeepAlive(uint32_t cid, uint8_t status = 0x01);

    void handleIncomingPacket(const uint8_t* buffer, uint16_t len);

    // Callbacks to CTAP2 core
    typedef void (*CborHandler)(uint32_t cid, const uint8_t* req, uint16_t reqLen);
    void setCborHandler(CborHandler handler) { _cborHandler = handler; }

private:
    uint32_t allocateCid();
    void handleInit(uint32_t cid, const uint8_t* payload, uint16_t len);
    void handlePing(uint32_t cid, const uint8_t* payload, uint16_t len);
    void handleWink(uint32_t cid);
    void dispatchMessage();

    uint32_t _nextCid;
    CtapMessage _rxMsg;
    bool _isReceiving;
    unsigned long _lastPacketTime;
    CborHandler _cborHandler;
    bool _diagnosticMode;
};

extern CtapHid ctapHid;
