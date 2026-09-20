#include "cbor_codec.h"
#include <string.h>

// ─── CBOR ENCODER ─────────────────────────────────────────────────────────────

CborEncoder::CborEncoder(uint8_t* buffer, size_t capacity) 
    : _buf(buffer), _cap(capacity), _offset(0) {}

bool CborEncoder::appendByte(uint8_t byte) {
    if (_offset >= _cap) return false;
    _buf[_offset++] = byte;
    return true;
}

bool CborEncoder::append(const uint8_t* data, size_t len) {
    if (_offset + len > _cap) return false;
    memcpy(_buf + _offset, data, len);
    _offset += len;
    return true;
}

bool CborEncoder::writeTypeAndValue(uint8_t majorType, uint64_t val) {
    uint8_t mt = majorType << 5;
    if (val < 24) {
        return appendByte(mt | (uint8_t)val);
    } else if (val <= 0xFF) {
        if (!appendByte(mt | 24)) return false;
        return appendByte((uint8_t)val);
    } else if (val <= 0xFFFF) {
        if (!appendByte(mt | 25)) return false;
        uint8_t b[2] = { (uint8_t)(val >> 8), (uint8_t)val };
        return append(b, 2);
    } else if (val <= 0xFFFFFFFF) {
        if (!appendByte(mt | 26)) return false;
        uint8_t b[4] = { (uint8_t)(val >> 24), (uint8_t)(val >> 16), (uint8_t)(val >> 8), (uint8_t)val };
        return append(b, 4);
    } else {
        if (!appendByte(mt | 27)) return false;
        uint8_t b[8] = {
            (uint8_t)(val >> 56), (uint8_t)(val >> 48), (uint8_t)(val >> 40), (uint8_t)(val >> 32),
            (uint8_t)(val >> 24), (uint8_t)(val >> 16), (uint8_t)(val >> 8),  (uint8_t)val
        };
        return append(b, 8);
    }
}

bool CborEncoder::encodeUnsigned(uint64_t val) {
    return writeTypeAndValue(0, val);
}

bool CborEncoder::encodeNegative(int64_t val) {
    uint64_t v = (uint64_t)(-1 - val);
    return writeTypeAndValue(1, v);
}

bool CborEncoder::encodeInt(int64_t val) {
    if (val >= 0) return encodeUnsigned((uint64_t)val);
    return encodeNegative(val);
}

bool CborEncoder::encodeBytes(const uint8_t* data, size_t len) {
    if (!writeTypeAndValue(2, len)) return false;
    return append(data, len);
}

bool CborEncoder::encodeText(const char* str) {
    size_t len = strlen(str);
    if (!writeTypeAndValue(3, len)) return false;
    return append((const uint8_t*)str, len);
}

bool CborEncoder::encodeArrayHeader(size_t count) {
    return writeTypeAndValue(4, count);
}

bool CborEncoder::encodeMapHeader(size_t count) {
    return writeTypeAndValue(5, count);
}

bool CborEncoder::encodeBool(bool val) {
    return appendByte(val ? 0xF5 : 0xF4);
}

bool CborEncoder::encodeNull() {
    return appendByte(0xF6);
}


// ─── CBOR DECODER ─────────────────────────────────────────────────────────────

CborDecoder::CborDecoder(const uint8_t* buffer, size_t length)
    : _buf(buffer), _len(length), _offset(0) {}

bool CborDecoder::peekType(uint8_t* majorType) {
    if (_offset >= _len) return false;
    *majorType = (_buf[_offset] >> 5) & 0x07;
    return true;
}

bool CborDecoder::readTypeAndValue(uint8_t* majorType, uint64_t* val) {
    if (_offset >= _len) return false;
    uint8_t initial = _buf[_offset++];
    *majorType = (initial >> 5) & 0x07;
    uint8_t info = initial & 0x1F;

    if (info < 24) {
        *val = info;
        return true;
    } else if (info == 24) {
        if (_offset >= _len) return false;
        *val = _buf[_offset++];
        return true;
    } else if (info == 25) {
        if (_offset + 2 > _len) return false;
        *val = ((uint64_t)_buf[_offset] << 8) | _buf[_offset + 1];
        _offset += 2;
        return true;
    } else if (info == 26) {
        if (_offset + 4 > _len) return false;
        *val = ((uint64_t)_buf[_offset] << 24) | ((uint64_t)_buf[_offset + 1] << 16) |
               ((uint64_t)_buf[_offset + 2] << 8) | _buf[_offset + 3];
        _offset += 4;
        return true;
    } else if (info == 27) {
        if (_offset + 8 > _len) return false;
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) {
            v = (v << 8) | _buf[_offset++];
        }
        *val = v;
        return true;
    }
    return false;
}

bool CborDecoder::readMapHeader(size_t* count) {
    uint8_t mt;
    uint64_t val;
    if (!readTypeAndValue(&mt, &val) || mt != 5) return false;
    *count = (size_t)val;
    return true;
}

bool CborDecoder::readArrayHeader(size_t* count) {
    uint8_t mt;
    uint64_t val;
    if (!readTypeAndValue(&mt, &val) || mt != 4) return false;
    *count = (size_t)val;
    return true;
}

bool CborDecoder::readUnsigned(uint64_t* val) {
    uint8_t mt;
    if (!readTypeAndValue(&mt, val) || mt != 0) return false;
    return true;
}

bool CborDecoder::readNegative(int64_t* val) {
    uint8_t mt;
    uint64_t raw;
    if (!readTypeAndValue(&mt, &raw) || mt != 1) return false;
    *val = -1 - (int64_t)raw;
    return true;
}

bool CborDecoder::readInt(int64_t* val) {
    uint8_t mt;
    uint64_t raw;
    if (!readTypeAndValue(&mt, &raw)) return false;
    if (mt == 0) {
        *val = (int64_t)raw;
        return true;
    } else if (mt == 1) {
        *val = -1 - (int64_t)raw;
        return true;
    }
    return false;
}

bool CborDecoder::readBytes(const uint8_t** data, size_t* len) {
    uint8_t mt;
    uint64_t val;
    if (!readTypeAndValue(&mt, &val) || mt != 2) return false;
    if (_offset + val > _len) return false;
    *data = _buf + _offset;
    *len = (size_t)val;
    _offset += (size_t)val;
    return true;
}

bool CborDecoder::readText(char* outStr, size_t maxLen) {
    uint8_t mt;
    uint64_t val;
    if (!readTypeAndValue(&mt, &val) || mt != 3) return false;
    if (_offset + val > _len) return false;
    size_t copyLen = (val < maxLen - 1) ? (size_t)val : (maxLen - 1);
    memcpy(outStr, _buf + _offset, copyLen);
    outStr[copyLen] = '\0';
    _offset += (size_t)val;
    return true;
}

bool CborDecoder::readBool(bool* val) {
    if (_offset >= _len) return false;
    uint8_t b = _buf[_offset++];
    if (b == 0xF5) { *val = true; return true; }
    if (b == 0xF4) { *val = false; return true; }
    return false;
}

static const size_t MAX_CBOR_DEPTH = 8;

bool CborDecoder::skipValue() {
    return skipValueInternal(0);
}

bool CborDecoder::skipValueInternal(size_t depth) {
    if (depth > MAX_CBOR_DEPTH) return false; // Prevent stack exhaustion

    uint8_t mt;
    uint64_t val;
    if (!readTypeAndValue(&mt, &val)) return false;
    switch (mt) {
        case 0:
        case 1:
            return true;
        case 2:
        case 3:
            if (_offset + val > _len) return false;
            _offset += (size_t)val;
            return true;
        case 4: // Array
            if (val > (_len - _offset)) return false; // An array cannot have more elements than remaining bytes
            for (size_t i = 0; i < val; i++) {
                if (!skipValueInternal(depth + 1)) return false;
            }
            return true;
        case 5: // Map (key + value pairs)
            if (val > (_len - _offset) / 2) return false; // A map requires at least 2 bytes per key-value pair
            for (size_t i = 0; i < val * 2; i++) {
                if (!skipValueInternal(depth + 1)) return false;
            }
            return true;
        case 7: // Simple / float
            return true;
        default:
            return false;
    }
}
