// Host-test in-memory filesystem stub
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include "Arduino.h"

#define FILE_READ   "r"
#define FILE_WRITE  "w"
#define FILE_APPEND "a"

namespace hoststub {

struct InMemFile {
    std::string path;
    std::vector<uint8_t> data;
    bool exists = true;
};

// Global in-memory storage for SD/FS
inline std::map<std::string, std::shared_ptr<InMemFile>> sd_files;
inline std::set<std::string> sd_directories;

// Persistent record of every byte written per path (preserved even after file removal)
inline std::map<std::string, std::vector<uint8_t>> final_write_record;

// Write failure injection flags
inline bool sd_fail_writes = false;
inline std::string sd_fail_write_path = "";
inline std::string sd_fail_open_path = "";
inline std::string sd_fail_rename_to = "";   // the next rename() onto this path fails (one-shot)

// Reset all filesystem state
inline void sd_reset() {
    sd_files.clear();
    sd_directories.clear();
    final_write_record.clear();
    sd_fail_writes = false;
    sd_fail_write_path.clear();
    sd_fail_open_path.clear();
    sd_fail_rename_to.clear();
}

} // namespace hoststub

class File {
public:
    File() : _pos(0), _valid(false) {}
    File(const std::string& path, const std::string& mode)
        : _path(path), _mode(mode), _pos(0), _valid(false) {
        if (!hoststub::sd_fail_open_path.empty() && path == hoststub::sd_fail_open_path) {
            return;
        }
        if (mode == "r") {
            auto it = hoststub::sd_files.find(path);
            if (it != hoststub::sd_files.end() && it->second->exists) {
                _file = it->second;
                _valid = true;
                _pos = 0;
            }
        } else if (mode == "w" || mode == "w+") {
            // Truncate if exists, or create new file
            auto it = hoststub::sd_files.find(path);
            if (it != hoststub::sd_files.end()) {
                _file = it->second;
                _file->data.clear();
                _file->exists = true;
            } else {
                _file = std::make_shared<hoststub::InMemFile>();
                _file->path = path;
                _file->exists = true;
                hoststub::sd_files[path] = _file;
            }
            _valid = true;
            _pos = 0;
        } else if (mode == "r+") {
            // Open for reading and writing without truncating; file must exist
            auto it = hoststub::sd_files.find(path);
            if (it != hoststub::sd_files.end() && it->second->exists) {
                _file = it->second;
                _valid = true;
                _pos = 0;
            }
        } else if (mode == "a" || mode == "a+") {
            auto it = hoststub::sd_files.find(path);
            if (it != hoststub::sd_files.end() && it->second->exists) {
                _file = it->second;
                _pos = _file->data.size();
            } else {
                _file = std::make_shared<hoststub::InMemFile>();
                _file->path = path;
                _file->exists = true;
                hoststub::sd_files[path] = _file;
                _pos = 0;
            }
            _valid = true;
        }
    }

    operator bool() const { return _valid; }
    bool operator!() const { return !_valid; }

    size_t write(const uint8_t* buf, size_t size) {
        if (!_valid || !_file || _mode == "r" || hoststub::sd_fail_writes) return 0;
        if (!hoststub::sd_fail_write_path.empty() && _path == hoststub::sd_fail_write_path) return 0;
        if (_pos + size > _file->data.size()) {
            _file->data.resize(_pos + size, 0);
        }
        memcpy(_file->data.data() + _pos, buf, size);

        // Record every byte written per path
        auto& fwr = hoststub::final_write_record[_path];
        if (_pos + size > fwr.size()) {
            fwr.resize(_pos + size, 0);
        }
        memcpy(fwr.data() + _pos, buf, size);

        _pos += size;
        return size;
    }

    size_t write(uint8_t b) { return write(&b, 1); }

    size_t read(uint8_t* buf, size_t size) {
        if (!_valid || !_file) return 0;
        if (_pos >= _file->data.size()) return 0;
        size_t avail = _file->data.size() - _pos;
        size_t n = (size < avail) ? size : avail;
        memcpy(buf, _file->data.data() + _pos, n);
        _pos += n;
        return n;
    }

    int read() {
        uint8_t b;
        return (read(&b, 1) == 1) ? b : -1;
    }

    size_t size() const {
        if (!_valid || !_file) return 0;
        return _file->data.size();
    }

    bool seek(uint32_t pos) {
        if (!_valid || !_file) return false;
        _pos = pos;
        return true;
    }

    uint32_t position() const { return (uint32_t)_pos; }

    void flush() {}

    void close() {
        _valid = false;
        _file.reset();
    }

    const char* name() const { return _path.c_str(); }

private:
    std::string _path;
    std::string _mode;
    size_t _pos = 0;
    bool _valid = false;
    std::shared_ptr<hoststub::InMemFile> _file;
};

class FS {
public:
    virtual ~FS() = default;

    virtual bool exists(const char* path) {
        auto it = hoststub::sd_files.find(path);
        if (it != hoststub::sd_files.end() && it->second->exists) {
            return true;
        }
        return hoststub::sd_directories.count(path) > 0;
    }

    virtual bool exists(const String& path) { return exists(path.c_str()); }

    virtual bool remove(const char* path) {
        auto it = hoststub::sd_files.find(path);
        if (it != hoststub::sd_files.end()) {
            it->second->exists = false;
            hoststub::sd_files.erase(it);
            return true;
        }
        return false;
    }

    virtual bool remove(const String& path) { return remove(path.c_str()); }

    virtual bool mkdir(const char* path) {
        hoststub::sd_directories.insert(path);
        return true;
    }

    virtual bool mkdir(const String& path) { return mkdir(path.c_str()); }

    virtual bool rename(const char* pathFrom, const char* pathTo) {
        if (!hoststub::sd_fail_rename_to.empty() && hoststub::sd_fail_rename_to == pathTo) {
            hoststub::sd_fail_rename_to.clear();
            return false;
        }
        auto it = hoststub::sd_files.find(pathFrom);
        if (it == hoststub::sd_files.end() || !it->second->exists) {
            return false;
        }
        auto filePtr = it->second;
        filePtr->path = pathTo;
        hoststub::sd_files.erase(it);
        hoststub::sd_files[pathTo] = filePtr;

        auto recIt = hoststub::final_write_record.find(pathFrom);
        if (recIt != hoststub::final_write_record.end()) {
            hoststub::final_write_record[pathTo] = recIt->second;
        }
        return true;
    }

    virtual bool rename(const String& pathFrom, const String& pathTo) {
        return rename(pathFrom.c_str(), pathTo.c_str());
    }

    virtual File open(const char* path, const char* mode = FILE_READ) {
        return File(path, mode);
    }

    virtual File open(const String& path, const char* mode = FILE_READ) {
        return open(path.c_str(), mode);
    }
};
