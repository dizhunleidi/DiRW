#ifndef DIMEMTOOL_MEMSEARCH_H
#define DIMEMTOOL_MEMSEARCH_H

#include "MemoryCore.hpp"

#include "baseRW.hpp"

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace DiMemTool {

// ── 全局搜索对齐值（字节） ──
// 默认 1 = 逐字节扫描（非对齐）
// 设为 4 则只扫描 4 字节对齐的地址，8 则 8 字节对齐
// 注意：g_memSearchAlign 是全局变量，多线程并发 setSearchAlign / 搜索 不安全 (B15)
extern size_t g_memSearchAlign;
inline void setSearchAlign(size_t align) {
    // B5: 0 退化为 1，防止内部 addr % align 触发除零 UB
    g_memSearchAlign = (align == 0) ? 1 : align;
}

// ── 独立搜索函数（无状态，直接调用） ──

// ── 普通版：返回地址数组 ──

// 字节模式搜索（带 ? 通配符的 mask，nullptr=全匹配）
std::vector<uintptr_t> memFindBytes(
    diRW::baseRW* rw,
    const AddressRange& range,
    const void* bytes, size_t size,
    const char* mask = nullptr);

// Hex 字符串搜索（如 "33 44 55 66"）
std::vector<uintptr_t> memFindHex(
    diRW::baseRW* rw,
    const AddressRange& range,
    const std::string& hex,
    const std::string& mask);

// IDA 风格模式搜索（如 "FF ?? 55 ?? 77"）
std::vector<uintptr_t> memFindIdaPattern(
    diRW::baseRW* rw,
    const AddressRange& range,
    const std::string& pattern);

// MapsEntry 重载
std::vector<uintptr_t> memFindBytes(diRW::baseRW* rw, const MapsEntry& entry,
                                    const void* bytes, size_t size,
                                    const char* mask = nullptr);
std::vector<uintptr_t> memFindHex(diRW::baseRW* rw, const MapsEntry& entry,
                                  const std::string& hex, const std::string& mask);
std::vector<uintptr_t> memFindIdaPattern(diRW::baseRW* rw, const MapsEntry& entry,
                                         const std::string& pattern);

// MapsEntry 数组重载
std::vector<uintptr_t> memFindBytes(diRW::baseRW* rw, const std::vector<MapsEntry>& regions,
                                    const void* bytes, size_t size,
                                    const char* mask = nullptr);
std::vector<uintptr_t> memFindHex(diRW::baseRW* rw, const std::vector<MapsEntry>& regions,
                                  const std::string& hex, const std::string& mask);
std::vector<uintptr_t> memFindIdaPattern(diRW::baseRW* rw, const std::vector<MapsEntry>& regions,
                                         const std::string& pattern);

// ── 详细版：返回 MemSearchResult（地址 + data），名加 Ex 后缀 ──

std::vector<MemSearchResult> memFindBytesEx(
    diRW::baseRW* rw,
    const AddressRange& range,
    const void* bytes, size_t size,
    const char* mask = nullptr);

std::vector<MemSearchResult> memFindHexEx(
    diRW::baseRW* rw,
    const AddressRange& range,
    const std::string& hex,
    const std::string& mask);

std::vector<MemSearchResult> memFindIdaPatternEx(
    diRW::baseRW* rw,
    const AddressRange& range,
    const std::string& pattern);

// MapsEntry 重载
std::vector<MemSearchResult> memFindBytesEx(diRW::baseRW* rw, const MapsEntry& entry,
                                            const void* bytes, size_t size,
                                            const char* mask = nullptr);
std::vector<MemSearchResult> memFindHexEx(diRW::baseRW* rw, const MapsEntry& entry,
                                          const std::string& hex, const std::string& mask);
std::vector<MemSearchResult> memFindIdaPatternEx(diRW::baseRW* rw, const MapsEntry& entry,
                                                 const std::string& pattern);

// MapsEntry 数组重载
std::vector<MemSearchResult> memFindBytesEx(diRW::baseRW* rw, const std::vector<MapsEntry>& regions,
                                            const void* bytes, size_t size,
                                            const char* mask = nullptr);
std::vector<MemSearchResult> memFindHexEx(diRW::baseRW* rw, const std::vector<MapsEntry>& regions,
                                          const std::string& hex, const std::string& mask);
std::vector<MemSearchResult> memFindIdaPatternEx(diRW::baseRW* rw, const std::vector<MapsEntry>& regions,
                                                 const std::string& pattern);

// ── Typed 搜索函数（返回 TypedSearchResult<T>，直接存取 typed value）──

template<typename T>
std::vector<TypedSearchResult<T>> memFindValue(
    diRW::baseRW* rw,
    const AddressRange& range,
    T value);

template<typename T>
std::vector<TypedSearchResult<T>> memFindValue(
    diRW::baseRW* rw,
    const MapsEntry& entry,
    T value);

template<typename T>
std::vector<TypedSearchResult<T>> memFindValue(
    diRW::baseRW* rw,
    const std::vector<MapsEntry>& regions,
    T value);

// ── Typed 搜索实现 ──

template<typename T>
inline std::vector<TypedSearchResult<T>> memFindValue(
    diRW::baseRW* rw, const AddressRange& range, T value)
{
    if (!rw || range.begin >= range.end) return {};

    std::vector<TypedSearchResult<T>> results;
    const size_t PAGE = 4096;
    const size_t valueSize = sizeof(T);

    if (valueSize == 0 || valueSize > PAGE + 4096) return results;

    size_t align = g_memSearchAlign;
    if (align == 0) align = 1;

    std::vector<uint8_t> buf(PAGE + valueSize - 1);
    std::vector<uint8_t> tail(valueSize - 1, 0);
    size_t tailLen = 0;

    uintptr_t addr = range.begin;
    while (addr < range.end) {
        size_t chunk = std::min(PAGE, (size_t)(range.end - addr));

        if (tailLen > 0)
            memcpy(buf.data(), tail.data(), tailLen);

        if (!rw->readv(addr, buf.data() + tailLen, chunk)) {
            addr += chunk;
            tailLen = 0;
            continue;
        }

        size_t searchLen = tailLen + chunk;
        uintptr_t baseAddr = addr - tailLen;

        size_t startOff = 0;
        if (align > 1) {
            uintptr_t mod = baseAddr % align;
            if (mod != 0) startOff = align - mod;
        }

        for (size_t off = startOff; off + valueSize <= searchLen; off += align) {
            T cur;
            memcpy(&cur, buf.data() + off, valueSize);
            if (cur == value) {
                results.emplace_back(baseAddr + off, value);
            }
        }

        size_t saveLen = (searchLen < valueSize - 1) ? searchLen : (valueSize - 1);
        if (saveLen > 0) {
            memcpy(tail.data(), buf.data() + searchLen - saveLen, saveLen);
            tailLen = saveLen;
        } else {
            tailLen = 0;
        }

        addr += chunk;
    }
    return results;
}

template<typename T>
inline std::vector<TypedSearchResult<T>> memFindValue(
    diRW::baseRW* rw, const MapsEntry& entry, T value)
{
    return memFindValue<T>(rw, entry.range(), value);
}

template<typename T>
inline std::vector<TypedSearchResult<T>> memFindValue(
    diRW::baseRW* rw, const std::vector<MapsEntry>& regions, T value)
{
    std::vector<TypedSearchResult<T>> all;
    size_t estimate = 0;
    for (const auto& e : regions) {
        if (e.isValid() && e.size() >= sizeof(T)) {
            estimate += e.size() / 64 + 1;
        }
    }
    all.reserve(estimate);
    for (const auto& e : regions) {
        if (!e.isValid() || e.size() < sizeof(T)) continue;
        auto r = memFindValue<T>(rw, e.range(), value);
        all.insert(all.end(), std::make_move_iterator(r.begin()), std::make_move_iterator(r.end()));
    }
    return all;
}

// ── MemSearch 类（有状态，分轮次搜索，typed 版） ──

template<typename T>
class MemSearch {
public:
    explicit MemSearch(diRW::baseRW* rw) : _rw(rw) {}
    ~MemSearch() = default;

    MemSearch(const MemSearch&) = delete;
    MemSearch& operator=(const MemSearch&) = delete;
    MemSearch(MemSearch&& other) noexcept : _rw(other._rw), _results(std::move(other._results)) {}
    MemSearch& operator=(MemSearch&& other) noexcept {
        if (this != &other) {
            _rw = other._rw;
            _results = std::move(other._results);
        }
        return *this;
    }

    // ── 首搜 ──
    size_t searchExact(const std::vector<MapsEntry>& regions, T value) {
        if (!_rw) return 0;
        _results.clear();
        struct Ctx { T target; };
        Ctx ctx{value};
        _results = scanRegions(regions,
            [](const T& val, const void* c) { return val == static_cast<const Ctx*>(c)->target; },
            &ctx);
        return _results.size();
    }

    size_t searchRange(const std::vector<MapsEntry>& regions, T minVal, T maxVal) {
        if (!_rw) return 0;
        if (minVal > maxVal) return 0;
        _results.clear();
        struct Ctx { T minV, maxV; };
        Ctx ctx{minVal, maxVal};
        _results = scanRegions(regions,
            [](const T& val, const void* c) {
                auto* p = static_cast<const Ctx*>(c);
                return val >= p->minV && val <= p->maxV;
            },
            &ctx);
        return _results.size();
    }

    size_t searchBiggerThan(const std::vector<MapsEntry>& regions, T value) {
        if (!_rw) return 0;
        _results.clear();
        _results = scanRegions(regions,
            [](const T& val, const void* c) { return val > *static_cast<const T*>(c); },
            &value);
        return _results.size();
    }

    size_t searchSmallerThan(const std::vector<MapsEntry>& regions, T value) {
        if (!_rw) return 0;
        _results.clear();
        _results = scanRegions(regions,
            [](const T& val, const void* c) { return val < *static_cast<const T*>(c); },
            &value);
        return _results.size();
    }

    // ── 精搜 ──
    size_t nextExact(T value) {
        struct Ctx { T target; };
        Ctx ctx{value};
        return refine(
            [](const T&, const T& cur, const void* c) { return cur == static_cast<const Ctx*>(c)->target; },
            &ctx, true);
    }

    size_t nextRange(T minVal, T maxVal) {
        if (minVal > maxVal) return 0;
        struct Ctx { T minV, maxV; };
        Ctx ctx{minVal, maxVal};
        return refine(
            [](const T&, const T& cur, const void* c) {
                auto* p = static_cast<const Ctx*>(c);
                return cur >= p->minV && cur <= p->maxV;
            },
            &ctx, true);
    }

    size_t nextBiggerThan(T value) {
        return refine(
            [](const T&, const T& cur, const void* c) { return cur > *static_cast<const T*>(c); },
            &value, true);
    }

    size_t nextSmallerThan(T value) {
        return refine(
            [](const T&, const T& cur, const void* c) { return cur < *static_cast<const T*>(c); },
            &value, true);
    }

    size_t nextIncreased() {
        return refine([](const T& old, const T& cur, const void*) { return cur > old; }, nullptr, true);
    }

    size_t nextDecreased() {
        return refine([](const T& old, const T& cur, const void*) { return cur < old; }, nullptr, true);
    }

    size_t nextChanged() {
        return refine([](const T& old, const T& cur, const void*) { return cur != old; }, nullptr, true);
    }

    size_t nextUnchanged() {
        return refine([](const T& old, const T& cur, const void*) { return cur == old; }, nullptr, true);
    }

    // ── 结果管理 ──
    const std::vector<TypedSearchResult<T>>& results() const { return _results; }
    void clear() { _results.clear(); }

    // ── 写入 ──
    bool writeResult(size_t index, T value) {
        if (index >= _results.size() || !_rw) return false;
        return _rw->writev(_results[index].address, &value, sizeof(T));
    }

    bool writeAddress(uintptr_t addr, T value) {
        if (!_rw) return false;
        return _rw->writev(addr, &value, sizeof(T));
    }

private:
    // 首搜：遍历 regions，逐页扫描，调 filter 匹配
    std::vector<TypedSearchResult<T>> scanRegions(
        const std::vector<MapsEntry>& regions,
        bool (*filter)(const T& val, const void* ctx),
        const void* ctx)
    {
        std::vector<TypedSearchResult<T>> results;
        const size_t PAGE = 4096;
        const size_t vsize = sizeof(T);

        if (vsize == 0 || vsize > PAGE + 4096) return results;

        size_t align = g_memSearchAlign;
        if (align == 0) align = 1;

        std::vector<uint8_t> buf(PAGE + vsize - 1);
        std::vector<uint8_t> tail(vsize - 1, 0);

        for (const auto& reg : regions) {
            if (!reg.isValid() || reg.size() < vsize) continue;

            uintptr_t addr = reg.start;
            uintptr_t end = reg.end;
            size_t tailLen = 0;

            while (addr < end) {
                size_t chunk = std::min(PAGE, (size_t)(end - addr));

                if (tailLen > 0)
                    memcpy(buf.data(), tail.data(), tailLen);

                if (!_rw->readv(addr, buf.data() + tailLen, chunk)) {
                    addr += chunk;
                    tailLen = 0;
                    continue;
                }

                size_t searchLen = tailLen + chunk;
                uintptr_t baseAddr = addr - tailLen;

                size_t startOff = 0;
                if (align > 1) {
                    uintptr_t mod = baseAddr % align;
                    if (mod != 0) startOff = align - mod;
                }

                for (size_t off = startOff; off + vsize <= searchLen; off += align) {
                    T cur;
                    memcpy(&cur, buf.data() + off, vsize);
                    if (filter(cur, ctx)) {
                        results.emplace_back(baseAddr + off, cur);
                    }
                }

                size_t saveLen = (searchLen < vsize - 1) ? searchLen : (vsize - 1);
                if (saveLen > 0) {
                    memcpy(tail.data(), buf.data() + searchLen - saveLen, saveLen);
                    tailLen = saveLen;
                } else {
                    tailLen = 0;
                }

                addr += chunk;
            }
        }
        return results;
    }

    // 精搜：重读已有结果的值，按 filter 过滤
    typedef bool (*FilterT)(const T& oldVal, const T& newVal, const void* ctx);

    size_t refine(FilterT filter, const void* ctx, bool updateOnMatch) {
        if (!_rw || _results.empty()) return 0;

        const size_t vsize = sizeof(T);
        std::vector<TypedSearchResult<T>> kept;

        for (auto& r : _results) {
            T cur;
            if (!_rw->readv(r.address, &cur, vsize))
                continue;

            if (filter(r.value, cur, ctx)) {
                if (updateOnMatch)
                    r.value = cur;
                kept.push_back(std::move(r));
            }
        }

        _results = std::move(kept);
        return _results.size();
    }

    diRW::baseRW* _rw;
    std::vector<TypedSearchResult<T>> _results;
};

} // namespace DiMemTool

#endif
