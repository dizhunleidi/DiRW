#ifndef DIMEMTOOL_PROCESSTOOL_H
#define DIMEMTOOL_PROCESSTOOL_H

namespace DiMemTool {

// 通过包名/进程名获取 PID
// 遍历 /proc 下所有进程，匹配 cmdline 内容
// 返回 PID，未找到返回 -1
int getPID(const char* packageName);

} // namespace DiMemTool

#endif
