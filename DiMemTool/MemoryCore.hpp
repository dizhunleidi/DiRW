#ifndef DIMEMTOOL_MEMORYCORE_H
#define DIMEMTOOL_MEMORYCORE_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

namespace DiMemTool {

// 地址范围：一个连续的轻量级地址范围
struct AddressRange {
    uintptr_t begin;
    uintptr_t end;

    size_t size() const { return end - begin; }
};

// 内存区域分类（对应 /proc/pid/maps 中典型的 Android 段）
enum class MemType {
    A,    // Anonymous RW（无路径的可读写内存）
    Xa,   // App 可执行代码段
    As,   // Ashmen（匿名共享内存）
    B,    // 字体
    Xs,   // 系统框架
    Ca,   // libc malloc
    Cb,   // .bss
    Cd,   // App 本地库
    J,    // Dalvik
    S,    // Stack
    V,    // GPU
    O     // Other
};

// 内存权限掩码
enum class MemPerms : uint8_t {
    None    = 0,
    Read    = 1 << 0,
    Write   = 1 << 1,
    Exec    = 1 << 2,
    Private = 1 << 3,
    Shared  = 1 << 4,
    RW      = Read | Write,
    RWX     = Read | Write | Exec,
};

constexpr MemPerms operator|(MemPerms a, MemPerms b) {
    return static_cast<MemPerms>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b)
    );
}

constexpr MemPerms operator&(MemPerms a, MemPerms b) {
    return static_cast<MemPerms>(
        static_cast<uint8_t>(a) & static_cast<uint8_t>(b)
    );
}

constexpr bool hasPerms(MemPerms value, MemPerms mask) {
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(mask)) == static_cast<uint8_t>(mask);
}

// 完整 maps 条目
struct MapsEntry {
    uintptr_t start;
    uintptr_t end;
    MemPerms perms;
    uintptr_t offset;
    unsigned long dev_major;
    unsigned long dev_minor;
    unsigned long inode;
    char path[256];

    // 段是否有效（地址非零且有长度）
    bool isValid() const {
        return start && end && end > start;
    }

    // 是否为匿名段（path 为空）
    bool isUnknown() const {
        return path[0] == '\0';
    }

    // 地址是否落在 [start, end) 内
    bool contains(uintptr_t addr) const {
        return addr >= start && addr < end;
    }

    // 返回本段对应的轻量地址范围
    AddressRange range() const {
        return {start, end};
    }

    // 本段大小（字节）
    size_t size() const { return end - start; }

    // 权限快捷判断
    bool is_private() const { return hasPerms(perms, MemPerms::Private); }
    bool is_shared()  const { return hasPerms(perms, MemPerms::Shared); }

    bool is_ro() const {
        return hasPerms(perms, MemPerms::Read) &&
              !hasPerms(perms, MemPerms::Write) &&
              !hasPerms(perms, MemPerms::Exec);
    }

    bool is_rw() const {
        return hasPerms(perms, MemPerms::Read) &&
               hasPerms(perms, MemPerms::Write);
    }

    bool is_rx() const {
        return hasPerms(perms, MemPerms::Read) &&
               hasPerms(perms, MemPerms::Exec);
    }
};

// ── rangeSize 工具函数 ──

inline size_t rangeSize(const AddressRange& r) {
    return r.size();
}

inline size_t rangeSize(const MapsEntry& e) {
    return e.size();
}

inline size_t rangeSize(const std::vector<AddressRange>& ranges) {
    size_t total = 0;
    for (const auto& r : ranges) total += r.size();
    return total;
}

inline size_t rangeSize(const std::vector<MapsEntry>& entries) {
    size_t total = 0;
    for (const auto& e : entries) total += e.size();
    return total;
}

// ── 搜索结果结构体 ──
// data 由 unique_ptr 自动管理，无需手动 free
struct MemSearchResult {
    uintptr_t                 address;
    std::unique_ptr<uint8_t[]> data;
    size_t                    size;
};

// ── Typed 搜索结果结构体 ──
// 直接存 typed value，省掉 unique_ptr + memcpy
template<typename T>
struct TypedSearchResult {
    uintptr_t address;
    T value;

    TypedSearchResult() = default;
    TypedSearchResult(uintptr_t addr, T val) : address(addr), value(val) {}
};

// ── MapsEntry 数组工具 ──

// 合并两个 MapsEntry 数组（operator+= 语法糖）
inline std::vector<MapsEntry>& operator+=(std::vector<MapsEntry>& lhs,
                                          const std::vector<MapsEntry>& rhs) {
    lhs.insert(lhs.end(), rhs.begin(), rhs.end());
    return lhs;
}

} // namespace DiMemTool

#endif
