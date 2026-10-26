#include "server/service.h"

#include "server/config.h"
#include "utils/common.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

#include <event2/event.h>
#include <event2/http.h>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


Service::Service()
{
    m_serverIp = Config::GetInstance().getServerIp();
    m_serverPort = Config::GetInstance().getServerPort();
    m_downloadPrefix = Config::GetInstance().getDownloadPrefix();
}

void Service::run()
{
    // 创建事件循环，使用 std::unique_ptr<T, Deleter> 独占管理资源。
    // 第一个模板参数 event_base 是对象类型，unique_ptr 内部保存 event_base*。
    // 第二个模板参数 decltype(&event_base_free) 获取删除器的类型，即 void (*)(event_base*)，不会执行该函数。
    // 两个构造参数分别是 event_base_new() 返回的资源指针，类型是 event_base *，以及 event_base_free 函数指针；后者保存起来，供释放资源时调用。
    // unique_ptr 默认使用 delete，这里指定 libevent 配套的释放函数；正常结束、提前 return 或因异常离开作用域时，自动释放非空指针。
    std::unique_ptr<event_base, decltype(&event_base_free)> base(event_base_new(), event_base_free);
    if (!base) throw std::runtime_error("Service::run(): event_base_new() failed");

    // base.get() 取出内部的原始指针供 libevent 使用，资源仍归 base 管理；http 使用同样的自定义删除器写法。局部变量按构造的反序销毁：先释放 HTTP 服务，再释放它依赖的 base。
    std::unique_ptr<evhttp, decltype(&evhttp_free)> http(evhttp_new(base.get()), evhttp_free);
    if (!http) throw std::runtime_error("Service::run(): evhttp_new() failed");

    // 使用配置中的监听地址和端口，Config 已校验端口范围。
    if (0 != evhttp_bind_socket(http.get(), m_serverIp.c_str(), static_cast<ev_uint16_t>(m_serverPort))) throw std::runtime_error("Service::run(): evhttp_bind_socket() failed for " + m_serverIp + ":" + std::to_string(m_serverPort));

    // evhttp_set_gencb() 注册通用请求回调（generic callback），由 libevent 收到请求后调用 GenericCallback；this 作为上下文传给 arg 参数。
    evhttp_set_gencb(http.get(), GenericCallback, this);

    // TODO: 后续处理 SIGINT/SIGTERM 停止信号，并补齐在途上传、下载请求的收尾。

    // 阻塞等待事件并执行回调；返回 -1 时抛出异常，0/1 分别表示正常退出或没有待处理事件。
    if (-1 == event_base_dispatch(base.get())) throw std::runtime_error("Service::run(): event_base_dispatch() failed");
}

void Service::GenericCallback(evhttp_request *, void *)
{
}

void Service::Upload(evhttp_request *, void *)
{
}

void Service::ListShow(evhttp_request *, void *)
{
}

void Service::Download(evhttp_request *, void *)
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

std::string Service::GenerateFileListHtml(const std::vector<FileMetadata> &files)
{
    std::ostringstream stream;
    stream << "<div class='file-list'><h3>已上传文件</h3>";

    const auto compressedDir = Config::GetInstance().getCompressedFilesDirPath();
    for (const auto &file : files)
    {
        const auto path = file.m_absoluteFilePath.lexically_normal();
        // 按配置的实际目录判断存储类型，避免根据文件名中的 compressed 等字样误判。
        const bool isCompressed = path == (compressedDir / path.filename()).lexically_normal();

        stream << "<div class='file-item'>"
               << "<div class='file-info'>"
               << "<span>📄" << utils::escapeHtmlText(path.filename().string()) << "</span>"
               << "<span class='file-type'>" << (isCompressed ? "压缩存储" : "普通存储") << "</span>"
               << "<span>存储大小：" << FormatFileSize(file.m_fileSize) << "</span>"
               << "<span>" << FormatFileTime(file.m_mtime) << "</span>"
               << "</div>"
               << "<button type='button' onclick=\"window.location='" << utils::urlEncode(file.m_url) << "'\">⬇️ 下载</button>"
               << "</div>";
    }

    stream << "</div>";
    if (stream.fail()) throw std::runtime_error("GenerateFileListHtml(): file list formatting failed");


    return stream.str();
}

std::string Service::GetETag(const FileMetadata &info)
{
    // ETag 是服务端为文件生成的版本标识，客户端和服务端通过它判断是否需要重新传输文件。
    // 1. 首次下载时，服务端返回文件内容和 ETag；客户端可以将正文与 ETag 一起缓存。
    // 2. 本地副本可用、再次需要验证版本时，客户端通过请求头把保存的 ETag 带回服务端。
    // 3. 服务端比较新旧 ETag：相同则返回“未修改”（304），不再发送正文，客户端复用本地缓存。
    // 4. 如果 ETag 不同，服务端返回新文件和新 ETag，客户端更新保存的内容和标签。
    // 5. 断点续传时，也用 ETag 确认版本一致后再传输剩余部分；版本改变则重新下载，避免拼接新旧内容。

    // 文件名中的特殊字节采用百分号编码，保证标签可以直接用于响应头。
    // 例如文件名是 "a b.txt"，原样拼接：ETag: "a b.txt-1024-123"，编码以后：ETag: "a%20b.txt-1024-123"。
    // HTTP 规范要求 ETag 的标签部分由双引号包裹，所以返回的字符串首尾加上 ""。
    std::string etag = "\"";

    etag += utils::fileNameEncode(info.m_absoluteFilePath.filename().string());
    etag += "-";
    etag += std::to_string(info.m_fileSize);
    etag += "-";
    etag += std::to_string(info.m_mtime.time_since_epoch().count());
    etag += '"';


    return etag;
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
