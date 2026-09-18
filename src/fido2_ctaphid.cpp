/**
 * fido2_ctaphid.cpp — Implementation of FIDO2 / CTAPHID Hardware Protocol
 */

#include "fido2_ctaphid.h"

void Fido2Ctaphid::begin() {
    _pendingUp = false;
    _nextCid = 0x01020304;
}

bool Fido2Ctaphid::processPacket(const uint8_t* in, uint8_t* out) {
    if (!in || !out) return false;
    memset(out, 0, CTAPHID_BUFFER_SIZE);

    uint32_t cid = (in[0] << 24) | (in[1] << 16) | (in[2] << 8) | in[3];
    uint8_t cmd = in[4];
    uint16_t length = (in[5] << 8) | in[6];

    // Echo CID back into response header
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    out[3] = in[3];
    out[4] = cmd; // Command response

    switch (cmd) {
        case CTAPHID_CMD_INIT:
            handleInit(in, out);
            return true;

        case CTAPHID_CMD_PING:
            handlePing(in, out);
            return true;

        case CTAPHID_CMD_WINK:
            handleWink(in, out);
            return true;

        case CTAPHID_CMD_CBOR:
        case CTAPHID_CMD_MSG:
            handleCbor(in, out);
            return true;

        default:
            // Unknown or unsupported command
            out[4] = CTAPHID_CMD_ERROR;
            out[5] = 0x00;
            out[6] = 0x01;
            out[7] = 0x01; // ERR_INVALID_CMD
            return true;
    }
}

void Fido2Ctaphid::handleInit(const uint8_t* in, uint8_t* out) {
    // Reply with allocated CID and device capabilities
    uint32_t newCid = _nextCid++;
    out[0] = in[0]; // If broadcast CID 0xFFFFFFFF, client accepts new CID
    out[1] = in[1];
    out[2] = in[2];
    out[3] = in[3];
    out[4] = CTAPHID_CMD_INIT;

    out[5] = 0x00;
    out[6] = 17; // Payload length: 8 nonce + 4 cid + 4 version + 1 capability

    // Nonce (8 bytes echoed back from input offset 7)
    memcpy(out + 7, in + 7, 8);

    // Allocated CID (4 bytes)
    out[15] = (newCid >> 24) & 0xFF;
    out[16] = (newCid >> 16) & 0xFF;
    out[17] = (newCid >> 8)  & 0xFF;
    out[18] = newCid & 0xFF;

    // CTAPHID Interface Version (2)
    out[19] = 0x02;
    // Major, Minor, Build Version (1.0.0)
    out[20] = 0x01;
    out[21] = 0x00;
    out[22] = 0x00;

    // Capabilities: 0x01 (WINK) | 0x04 (CBOR support)
    out[23] = 0x05;

    Serial.printf("[CTAPHID] INIT received. Allocated CID 0x%08X (Capabilities: CBOR+WINK)\n", newCid);
}

void Fido2Ctaphid::handlePing(const uint8_t* in, uint8_t* out) {
    uint16_t len = (in[5] << 8) | in[6];
    if (len > 57) len = 57;
    out[5] = 0x00;
    out[6] = (uint8_t)len;
    memcpy(out + 7, in + 7, len);
}

void Fido2Ctaphid::handleWink(const uint8_t* in, uint8_t* out) {
    out[5] = 0x00;
    out[6] = 0x00;
    Serial.println("[CTAPHID] WINK received! Flashing LED beacon.");
}

void Fido2Ctaphid::handleCbor(const uint8_t* in, uint8_t* out) {
    // WebAuthn request requiring User Presence!
    _pendingUp = true;
    _upStartTime = millis();

    // Default response: Keep-alive status (waiting for user presence)
    out[4] = CTAPHID_CMD_CBOR;
    out[5] = 0x00;
    out[6] = 0x01;
    out[7] = 0x02; // CTAP2_ERR_USER_PRESENCE_REQUIRED or keepalive status

    Serial.println("[CTAPHID] CBOR Assertion requested. Prompting User Presence on screen!");
}

void Fido2Ctaphid::confirmUserPresence() {
    _pendingUp = false;
    Serial.println("[CTAPHID] User Presence CONFIRMED via physical button press.");
}

void Fido2Ctaphid::rejectUserPresence() {
    _pendingUp = false;
    Serial.println("[CTAPHID] User Presence REJECTED by user.");
}
