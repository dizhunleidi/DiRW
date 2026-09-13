#include "ProcessTool.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dirent.h>

namespace DiMemTool {

int getPID(const char* packageName) {
    DIR* dir = opendir("/proc");
    if (!dir) return -1;

    int id;
    FILE* fp;
    char filename[64];
    char cmdline[256];
    struct dirent* entry;

    while ((entry = readdir(dir)) != nullptr) {
        id = atoi(entry->d_name);
        if (id <= 0) continue;

        snprintf(filename, sizeof(filename), "/proc/%d/cmdline", id);
        fp = fopen(filename, "r");
        if (!fp) continue;

        if (fgets(cmdline, sizeof(cmdline), fp)) {
            fclose(fp);
            if (strcmp(packageName, cmdline) == 0) {
                closedir(dir);
                return id;
            }
        } else {
            fclose(fp);
        }
    }

    closedir(dir);
    return -1;
}

} // namespace DiMemTool
