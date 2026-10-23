#include "server/service.h"

#include "server/config.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


Service::Service()
{
    m_serverIp = Config::GetInstance().getServerIp();
    m_serverPort = Config::GetInstance().getServerPort();
    m_downloadPrefix = Config::GetInstance().getDownloadPrefix();
}

bool Service::runModule()
{
}

void Service::GenHandler(evhttp_request *, void *)
{
}

void Service::Upload(evhttp_request *, void *)
{
}

std::string Service::FormatFileTime(std::filesystem::file_time_type t)
{
    // 主要逻辑参考：https://github.com/DavidingPlus/dlog/blob/master/src/utils/timestamp.cpp 的 Timestamp::toFormattedString() 函数。

    // t 是文件修改时间点，使用文件系统时钟表示；转换时保留它所代表的修改时刻。fileNow 使用文件系统时钟 std::filesystem::file_time_type::clock，与 t 属于同一种时钟，可以相减，得到“修改时刻距离现在”的时间间隔。
    auto fileNow = std::filesystem::file_time_type::clock::now();
    // system_clock 是系统时钟类型，now() 返回系统时钟下的当前时间点 systemNow。它与文件系统时钟的计时起点可能不同，不能直接把文件时间的计数当作系统时间使用。
    auto systemNow = std::chrono::system_clock::now();
    // t - fileNow 是时间间隔，例如文件在 10 分钟前修改，结果就是 -10 分钟。systemNow 加上该间隔，得到系统时钟下对应的文件修改时间点 systemTime。
    auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(systemNow + (t - fileNow));

    auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(systemTime);
    // 对负时间戳，time_point_cast 会向 0 方向截断。例如 tp = -0.2s 时，截断后的 seconds 会先变成 0s，导致后面的子秒部分变成 -0.2s。这里把整秒部分回退到 -1s，最终就能拆成 (-1s) + 0.8s，保证纳秒部分始终落在 [0, 1s)。
    if (seconds > systemTime) seconds -= std::chrono::seconds(1);

    auto time = std::chrono::system_clock::to_time_t(seconds);

    // 不使用 std::localtime，因为它返回指向共享静态缓冲区的指针：多个线程同时调用时，某个线程可能覆盖另一个线程刚刚得到的日期时间。
    // 这里每次调用都使用自己的局部 tm 对象，再通过 localtime_s/localtime_r 把转换结果写入这个对象。
    std::tm localTime{};
    if (!::localtime_r(&time, &localTime)) throw std::runtime_error("FormatFileTime(): localtime_r() failed");

    // stream 收集格式化后的字，std::put_time() 按指定格式输出日历字段，str() 返回最终字符串。
    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    if (stream.fail()) throw std::runtime_error("FormatFileTime(): file time formatting failed");


    return stream.str();
}

std::string Service::FormatFileSize(uint64_t bytes)
{
    // 每 1024 字节进一档，最大使用 GB，显示时固定保留两位小数。
    constexpr const char *units[] = {"B", "KB", "MB", "GB"};
    int index = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024 && index < 3)
    {
        size /= 1024;
        ++index;
    }

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << size << " " << units[index];
    if (stream.fail()) throw std::runtime_error("FormatFileSize(): file size formatting failed");


    return stream.str();
}

std::string Service::GenerateModernFileList(const std::vector<FileMetadata> &)
{
}

void Service::ListShow(evhttp_request *, void *)
{
}

std::string Service::GetETag(const FileMetadata &)
{
}

void Service::Download(evhttp_request *, void *)
{
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
