#include "MemPatch.hpp"
#include "../diRW/baseRW.hpp"

#include <cstring>
#include <cstdio>
#include <sstream>
#include <iomanip>

namespace DiMemTool {

MemPatch::MemPatch()
    : _rw(nullptr), _address(0) {}

MemPatch::MemPatch(diRW::baseRW* rw, uintptr_t address,
                   const void* patchCode, size_t patchSize)
    : _rw(rw), _address(address) {

    if (!rw || !address || !patchCode || patchSize == 0) return;

    _patchCode.assign((const uint8_t*)patchCode,
                      (const uint8_t*)patchCode + patchSize);

    _origCode.resize(patchSize);
    rw->readv(address, _origCode.data(), patchSize);
}

MemPatch::~MemPatch() = default;

MemPatch::MemPatch(MemPatch&& other) noexcept
    : _rw(other._rw), _address(other._address),
      _origCode(std::move(other._origCode)),
      _patchCode(std::move(other._patchCode)) {
    other._rw = nullptr;
    other._address = 0;
}

MemPatch& MemPatch::operator=(MemPatch&& other) noexcept {
    if (this != &other) {
        _rw = other._rw;
        _address = other._address;
        _origCode = std::move(other._origCode);
        _patchCode = std::move(other._patchCode);
        other._rw = nullptr;
        other._address = 0;
    }
    return *this;
}

bool MemPatch::isValid() const {
    return _rw && _address && !_origCode.empty() && !_patchCode.empty();
}

size_t MemPatch::size() const { return _patchCode.size(); }

uintptr_t MemPatch::address() const { return _address; }

bool MemPatch::apply() {
    if (!isValid()) return false;
    return _rw->writev(_address, _patchCode.data(), _patchCode.size());
}

bool MemPatch::restore() {
    if (!isValid()) return false;
    return _rw->writev(_address, _origCode.data(), _origCode.size());
}

static std::string bytesToHex(const uint8_t* data, size_t size) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0');
    for (size_t i = 0; i < size; ++i) {
        if (i > 0) oss << ' ';
        oss << std::setw(2) << (unsigned)data[i];
    }
    return oss.str();
}

std::string MemPatch::currentBytes() const {
    if (!_rw || !_address) return {};
    std::vector<uint8_t> buf(_patchCode.size());
    if (!_rw->readv(_address, buf.data(), buf.size()))
        return {};
    return bytesToHex(buf.data(), buf.size());
}

std::string MemPatch::originalBytes() const {
    return bytesToHex(_origCode.data(), _origCode.size());
}

std::string MemPatch::patchBytes() const {
    return bytesToHex(_patchCode.data(), _patchCode.size());
}

MemPatch MemPatch::createWithBytes(diRW::baseRW* rw, uintptr_t addr,
                                    const void* code, size_t size) {
    return MemPatch(rw, addr, code, size);
}

static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    std::string s;
    for (char c : hex) {
        if (c == ' ') continue;
        s += c;
    }
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        bytes.push_back((uint8_t)strtoul(s.substr(i, 2).c_str(), nullptr, 16));
    }
    return bytes;
}

MemPatch MemPatch::createWithHex(diRW::baseRW* rw, uintptr_t addr,
                                  const std::string& hex) {
    auto bytes = hexToBytes(hex);
    if (bytes.empty()) return {};
    return MemPatch(rw, addr, bytes.data(), bytes.size());
}

} // namespace DiMemTool
