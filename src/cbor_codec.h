#pragma once
#include <Arduino.h>

/**
 * Lightweight Zero-Allocation CBOR Encoder & Decoder for CTAP2 / WebAuthn
 * Compliant with RFC 8949 and FIDO2 CTAP2 specification
 */

class CborEncoder {
public:
    CborEncoder(uint8_t* buffer, size_t capacity);
    
    size_t getLength() const { return _offset; }
    const uint8_t* getBuffer() const { return _buf; }
    
    bool encodeUnsigned(uint64_t val);
    bool encodeNegative(int64_t val);
    bool encodeInt(int64_t val);
    bool encodeBytes(const uint8_t* data, size_t len);
    bool encodeText(const char* str);
    bool encodeArrayHeader(size_t count);
    bool encodeMapHeader(size_t count);
    bool encodeBool(bool val);
    bool encodeNull();

private:
    bool writeTypeAndValue(uint8_t majorType, uint64_t val);
    bool append(const uint8_t* data, size_t len);
    bool appendByte(uint8_t byte);

    uint8_t* _buf;
    size_t   _cap;
    size_t   _offset;
};

class CborDecoder {
public:
    CborDecoder(const uint8_t* buffer, size_t length);

    bool hasMore() const { return _offset < _len; }
    size_t getRemaining() const { return _len - _offset; }

    bool peekType(uint8_t* majorType);
    bool readMapHeader(size_t* count);
    bool readArrayHeader(size_t* count);
    bool readUnsigned(uint64_t* val);
    bool readNegative(int64_t* val);
    bool readInt(int64_t* val);
    bool readBytes(const uint8_t** data, size_t* len);
    bool readText(char* outStr, size_t maxLen);
    bool readBool(bool* val);
    bool skipValue();

private:
    bool skipValueInternal(size_t depth);
    bool readTypeAndValue(uint8_t* majorType, uint64_t* val);
    const uint8_t* _buf;
    size_t _len;
    size_t _offset;
};
