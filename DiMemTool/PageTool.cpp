#include "PageTool.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <regex.h>
#include <algorithm>

namespace DiMemTool {

// ═══════════════════════════════════════════
//  工具函数
// ═══════════════════════════════════════════

MemPerms parsePerms(const char* flags) {
    MemPerms perms = MemPerms::None;
    for (const char* p = flags; *p; ++p) {
        switch (*p) {
            case 'r': perms = perms | MemPerms::Read;   break;
            case 'w': perms = perms | MemPerms::Write;  break;
            case 'x': perms = perms | MemPerms::Exec;   break;
            case 's': perms = perms | MemPerms::Shared;  break;
            case 'p': perms = perms | MemPerms::Private; break;
        }
    }
    return perms;
}

MemType classify(const char* path, const char* flags) {
    if (strstr(flags, "rw") && strlen(path) == 0)
        return MemType::A;
    if ((strstr(path, "/data/app/") && strstr(flags, "r-xp")) ||
        (strstr(flags, "r-xp") && strlen(path) == 0) ||
        (strstr(path, "/data/data/") && strstr(flags, "r-xp")))
        return MemType::Xa;
    if (strstr(path, "/dev/ashmem/"))
        return MemType::As;
    if (strstr(path, "/system/fonts/"))
        return MemType::B;
    if (strstr(path, "/system/framework/"))
        return MemType::Xs;
    if (strcmp(path, "[anon:libc_malloc]") == 0)
        return MemType::Ca;
    if (strstr(path, "[anon:.bss]"))
        return MemType::Cb;
    if ((strstr(path, "/data/app") && strstr(path, "/lib")) ||
        (strstr(path, "/data/data") && strstr(path, "/lib")))
        return MemType::Cd;
    if (strstr(path, "[anon:dalvik"))
        return MemType::J;
    if (strcmp(path, "[stack]") == 0)
        return MemType::S;
    if (strcmp(path, "/dev/kgsl-3d0") == 0)
        return MemType::V;
    return MemType::O;
}

MemType classify(const char* path, MemPerms perms) {
    if (hasPerms(perms, MemPerms::RW) && strlen(path) == 0)
        return MemType::A;
    if ((strstr(path, "/data/app/") && hasPerms(perms, MemPerms::Read | MemPerms::Exec)) ||
        (hasPerms(perms, MemPerms::Read | MemPerms::Exec) && strlen(path) == 0) ||
        (strstr(path, "/data/data/") && hasPerms(perms, MemPerms::Read | MemPerms::Exec)))
        return MemType::Xa;
    if (strstr(path, "/dev/ashmem/"))
        return MemType::As;
    if (strstr(path, "/system/fonts/"))
        return MemType::B;
    if (strstr(path, "/system/framework/"))
        return MemType::Xs;
    if (strcmp(path, "[anon:libc_malloc]") == 0)
        return MemType::Ca;
    if (strstr(path, "[anon:.bss]"))
        return MemType::Cb;
    if ((strstr(path, "/data/app") && strstr(path, "/lib")) ||
        (strstr(path, "/data/data") && strstr(path, "/lib")))
        return MemType::Cd;
    if (strstr(path, "[anon:dalvik"))
        return MemType::J;
    if (strcmp(path, "[stack]") == 0)
        return MemType::S;
    if (strcmp(path, "/dev/kgsl-3d0") == 0)
        return MemType::V;
    return MemType::O;
}

// ═══════════════════════════════════════════
//  PageTool
// ═══════════════════════════════════════════

PageTool::PageTool(int pid)
    : _pid(pid) {
    refresh();
}

bool PageTool::refresh() {
    _entries.clear();

    char path[128];
    snprintf(path, sizeof(path), "/proc/%d/maps", _pid);

    FILE* fp = fopen(path, "r");
    if (!fp) {
        printf("[DiMemTool] 无法打开 /proc/%d/maps\n", _pid);
        return false;
    }

    char* line = nullptr;
    size_t n = 0;

    while (getline(&line, &n, fp) > 0) {
        MapsEntry me;
        std::memset(&me, 0, sizeof(me));

        char perms_str[8] = {};
        char name[256] = {};

        int matched = sscanf(line, "%lx-%lx %7s %lx %lx:%lx %lu %255[^\n]",
                             &me.start, &me.end, perms_str,
                             &me.offset, &me.dev_major, &me.dev_minor,
                             &me.inode, name);

        if (matched < 7) continue;
        if (matched >= 8) {
            std::strncpy(me.path, name, sizeof(me.path) - 1);
        }

        me.perms = parsePerms(perms_str);
        _entries.push_back(me);
    }

    free(line);
    fclose(fp);
    return true;
}

// ── 路径名过滤 ──

std::vector<MapsEntry> PageTool::getMaps() const {
    return _entries;
}

std::vector<MapsEntry> PageTool::getMapsEqual(const std::string& name) const {
    std::vector<MapsEntry> result;
    for (const auto& e : _entries) {
        if (name == e.path) result.push_back(e);
    }
    return result;
}

std::vector<MapsEntry> PageTool::getMapsContaining(const std::string& substr) const {
    std::vector<MapsEntry> result;
    for (const auto& e : _entries) {
        if (strstr(e.path, substr.c_str())) result.push_back(e);
    }
    return result;
}

std::vector<MapsEntry> PageTool::getMapsStartingWith(const std::string& prefix) const {
    std::vector<MapsEntry> result;
    for (const auto& e : _entries) {
        if (strncmp(e.path, prefix.c_str(), prefix.size()) == 0) result.push_back(e);
    }
    return result;
}

std::vector<MapsEntry> PageTool::getMapsEndingWith(const std::string& suffix) const {
    std::vector<MapsEntry> result;
    for (const auto& e : _entries) {
        size_t plen = strlen(e.path);
        size_t slen = suffix.size();
        if (plen >= slen && strcmp(e.path + plen - slen, suffix.c_str()) == 0)
            result.push_back(e);
    }
    return result;
}

std::vector<MapsEntry> PageTool::getMapsByRegex(const std::string& pattern) const {
    std::vector<MapsEntry> result;

    regex_t re;
    if (regcomp(&re, pattern.c_str(), REG_EXTENDED | REG_NOSUB) != 0)
        return result;

    for (const auto& e : _entries) {
        if (regexec(&re, e.path, 0, nullptr, 0) == 0)
            result.push_back(e);
    }

    regfree(&re);
    return result;
}

// ── 其他过滤 ──

std::vector<MapsEntry> PageTool::getRegions(MemType type) const {
    std::vector<MapsEntry> result;
    for (const auto& e : _entries) {
        if (classify(e.path, e.perms) == type) {
            result.push_back(e);
        }
    }
    return result;
}

std::vector<MapsEntry> PageTool::getRegionsByPerms(MemPerms perms) const {
    std::vector<MapsEntry> result;
    for (const auto& e : _entries) {
        if (hasPerms(e.perms, perms)) {
            result.push_back(e);
        }
    }
    return result;
}

MapsEntry PageTool::getAddressMap(uintptr_t addr) const {
    // 二分查找：找到第一个 endAddress > addr 的条目
    auto it = std::lower_bound(_entries.begin(), _entries.end(), addr,
        [](const MapsEntry& m, uintptr_t val) {
            return m.end <= val;
        });

    if (it != _entries.end() && it->contains(addr))
        return *it;

    return {};
}

} // namespace DiMemTool
