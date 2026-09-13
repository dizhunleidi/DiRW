#ifndef DIMEMTOOL_MEMPATCH_H
#define DIMEMTOOL_MEMPATCH_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace diRW { class baseRW; }

namespace DiMemTool {

class MemPatch {
public:
    MemPatch();
    MemPatch(diRW::baseRW* rw, uintptr_t address,
             const void* patchCode, size_t patchSize);
    ~MemPatch();

    MemPatch(MemPatch&& other) noexcept;
    MemPatch& operator=(MemPatch&& other) noexcept;

    MemPatch(const MemPatch&) = delete;
    MemPatch& operator=(const MemPatch&) = delete;

    bool isValid() const;
    size_t size() const;
    uintptr_t address() const;

    bool apply();
    bool restore();

    std::string currentBytes() const;
    std::string originalBytes() const;
    std::string patchBytes() const;

    static MemPatch createWithBytes(diRW::baseRW* rw, uintptr_t addr,
                                    const void* code, size_t size);
    static MemPatch createWithHex(diRW::baseRW* rw, uintptr_t addr,
                                  const std::string& hex);

private:
    diRW::baseRW*      _rw;
    uintptr_t          _address;
    std::vector<uint8_t> _origCode;
    std::vector<uint8_t> _patchCode;
};

} // namespace DiMemTool

#endif
