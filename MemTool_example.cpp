#include <cstdio>
#include <cstring>
#include "DiMemTool/ProcessTool.hpp"
#include "DiMemTool/PageTool.hpp"
#include "DiMemTool/MemSearch.hpp"
#include "syscallRW.hpp"
#include "TGodRW.hpp"

using namespace DiMemTool;

int main()
{
    // 1. 查 PID
    int pid = getPID("bin.mt.plus");
    if (pid <= 0)
    {
        printf("未找到进程\n");
        return 1;
    }
    printf("PID = %d\n\n", pid);

    // 2. 构造 PageTool + 读写后端
    PageTool pt(pid);
    diRW::TGodRW rw(diRW::baseRW::PidMode::Private, pid);

    if(rw.isConnected()){
        printf("驱动连接成功 \n");
    } else {
        printf("驱动连接失败 \n");
        return 0;
    }

    // 3. 拿到匿名 RW + libc_malloc 内存段
    auto regions = pt.getRegions(MemType::A);
    regions += pt.getRegions(MemType::Ca);//可以直接添加内存段
    printf("A+Ca 段数: %zu, 总大小: %zu MB\n",
           regions.size(), rangeSize(regions) / 1024 / 1024);

    // str搜索测试
    const char *target = u8"www";
    setSearchAlign(1); // 设置搜索对齐为 1 字节
    auto result_str = memFindBytes(&rw, regions, target, strlen(target));
    printf("str测试命中数: %zu\n", result_str.size());

    for (size_t i = 0; i < result_str.size() && i < 5; ++i)
    {
        printf("  %016lx\n", result_str[i]);
    }
    if (result_str.size() > 5)
    {
        printf("  ...\n");
    }

    // int搜索测试（typed 方式）
    int test = 16384;
    setSearchAlign(4); // 设置搜索对齐为 4 字节
    auto result_int = memFindValue<int32_t>(&rw, regions, test);
    printf("int测试命中数: %zu\n", result_int.size());
    for (size_t i = 0; i < result_int.size() && i < 5; ++i)
    {
        printf("  %016lx = %d\n", result_int[i].address, result_int[i].value);
    }
    if (result_int.size() > 5)
    {
        printf("  ...\n");
    }

    // type int 搜索测试
    auto type_result = memFindValue<int>(&rw, regions, 16384);
    printf("typeint测试命中数: %zu\n", type_result.size());
    for (size_t i = 0; i < type_result.size() && i < 5; ++i)
    {
        printf("  %016lx = %ld\n", type_result[i].address, type_result[i].value);
    }
    if (type_result.size() > 5)
    {
        printf("  ...\n");
    }

    return 0;
}
