/**
 * fido2_ctaphid.h — FIDO2 / CTAPHID Hardware Security Key Engine
 * ===============================================================
 * Provides the USB HID 64-byte report framing and protocol responses
 * for WebAuthn, Terminus, Chrome, and OpenSSH FIDO keys.
 */

#pragma once

#include <Arduino.h>

#define CTAPHID_BUFFER_SIZE 64

// CTAPHID Commands (FIDO Alliance Standard)
#define CTAPHID_CMD_PING    0x81
#define CTAPHID_CMD_MSG     0x83
#define CTAPHID_CMD_LOCK    0x84
#define CTAPHID_CMD_INIT    0x86
#define CTAPHID_CMD_WINK    0x88
#define CTAPHID_CMD_CBOR    0x90
#define CTAPHID_CMD_CANCEL  0x91
#define CTAPHID_CMD_ERROR   0xBF

// FIDO Report Descriptor (Usage Page 0xF1D0, Usage 0x01)
static const uint8_t fido_report_descriptor[] = {
    0x06, 0xD0, 0xF1, // USAGE_PAGE (FIDO Alliance)
    0x09, 0x01,       // USAGE (U2F / CTAP HID Authenticator Device)
    0xA1, 0x01,       // COLLECTION (Application)
    0x09, 0x20,       //   USAGE (Data In)
    0x15, 0x00,       //   LOGICAL_MINIMUM (0)
    0x26, 0xFF, 0x00, //   LOGICAL_MAXIMUM (255)
    0x75, 0x08,       //   REPORT_SIZE (8)
    0x95, 0x40,       //   REPORT_COUNT (64 bytes)
    0x81, 0x02,       //   INPUT (Data, Var, Abs)
    0x09, 0x21,       //   USAGE (Data Out)
    0x15, 0x00,       //   LOGICAL_MINIMUM (0)
    0x26, 0xFF, 0x00, //   LOGICAL_MAXIMUM (255)
    0x75, 0x08,       //   REPORT_SIZE (8)
    0x95, 0x40,       //   REPORT_COUNT (64 bytes)
    0x91, 0x02,       //   OUTPUT (Data, Var, Abs)
    0xC0              // END_COLLECTION
};

struct CtaphidInitResponse {
    uint8_t  nonce[8];
    uint32_t cid;
    uint8_t  versionInterface;
    uint8_t  versionMajor;
    uint8_t  versionMinor;
    uint8_t  versionBuild;
    uint8_t  capabilities; // 0x01 = WINK, 0x04 = CBOR
};

class Fido2Ctaphid {
public:
    void begin();
    bool processPacket(const uint8_t* inPacket, uint8_t* outPacket);

    bool isPendingUserPresence() const { return _pendingUp; }
    const char* getRpDomain() const { return _rpDomain; }
    void confirmUserPresence();
    void rejectUserPresence();

private:
    void handleInit(const uint8_t* in, uint8_t* out);
    void handlePing(const uint8_t* in, uint8_t* out);
    void handleWink(const uint8_t* in, uint8_t* out);
    void handleCbor(const uint8_t* in, uint8_t* out);

    uint32_t _nextCid = 0x01020304;
    bool     _pendingUp = false;
    char     _rpDomain[48] = "terminus.app";
    uint32_t _upStartTime = 0;
};
