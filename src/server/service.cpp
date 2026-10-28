#include "server/service.h"

#include "server/config.h"
#include "utils/common.h"
#include "utils/fileutil.h"
#include "utils/jsonutil.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>

#include <base64.h>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


namespace
{

    StorageType parseStorageType(const std::string &storageTypeText)
    {
        if ("uncompressed" == storageTypeText)
        {
            return StorageType::Uncompressed;
        }
        if ("compressed" == storageTypeText)
        {
            return StorageType::Compressed;
        }
        else
        {
            throw std::invalid_argument("parseStorageType(): unsupported storage type: " + storageTypeText);
        }
    }

}


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
    if (0 != evhttp_bind_socket(http.get(), m_serverIp.data(), static_cast<ev_uint16_t>(m_serverPort))) throw std::runtime_error("Service::run(): evhttp_bind_socket() failed for " + m_serverIp + ":" + std::to_string(m_serverPort));

    // evhttp_set_gencb() 注册通用请求回调（generic callback），由 libevent 收到请求后调用 GenericCallback；this 作为上下文传给 arg 参数。
    evhttp_set_gencb(http.get(), GenericCallback, this);

    // TODO: 后续处理 SIGINT/SIGTERM 停止信号，并补齐在途上传、下载请求的收尾。

    // 阻塞等待事件并执行回调；返回 -1 时抛出异常，0/1 分别表示正常退出或没有待处理事件。
    if (-1 == event_base_dispatch(base.get())) throw std::runtime_error("Service::run(): event_base_dispatch() failed");
}

void Service::GenericCallback(evhttp_request *req, void *arg)
{
    // evhttp_request_get_evhttp_uri() 取出 req 中已解析的 URI 对象；evhttp_uri_get_path() 读取路径，不含查询参数 query。路径中的 %HH 由 urlDecode() 还原，具体见 docs/libevent-request-uri.md。
    std::string path = utils::urlDecode(evhttp_uri_get_path(evhttp_request_get_evhttp_uri(req)));

    // run() 注册回调时传入 this 指针。
    Service *service = static_cast<Service *>(arg);

    // 下载请求。
    if (std::string::npos != path.find(service->m_downloadPrefix))
    {
        Download(req, arg);
    }
    // 上传请求。
    else if ("/upload" == path)
    {
        Upload(req, arg);
    }
    // 显示已存储文件列表，返回一个 HTML 页面给浏览器。
    else if ("/" == path)
    {
        ShowFileList(req, arg);
    }
    // 404。
    else
    {
        evhttp_send_reply(req, HTTP_NOTFOUND, "Not Found", nullptr);
    }
}

void Service::Upload(evhttp_request *req, void *arg)
{
    // 整体流程：先校验，再读取正文和保存。
    // 1. 检查 HTTP 方法，上传只接受 POST。
    // 2. 读取请求头，检查字段是否缺失或为空，并将存储策略解析为 StorageType 枚举。
    // 3. 解码文件名。
    // 4. 根据 StorageType 枚举选择未压缩或压缩目录，拼出最终文件路径。
    // 5. 检查下载 URL 和磁盘路径是否已被占用，避免覆盖已有数据。
    // 6. 校验通过后，复制请求体中的原始文件字节，支持二进制内容和空文件。
    // 7. 创建目录，未压缩策略直接保存，压缩策略按 bundle_format 压缩后保存。
    // 8. 根据实际文件生成元数据，准备响应 JSON，再插入并持久化元数据。
    // 9. 文件和元数据都保存成功后，返回 201 和 JSON。
    // 保存失败时，DataManager 回滚新增记录，本函数尝试清理本次上传文件并返回 500。

    // 1. 检查 HTTP 方法，上传只接受 POST。其他方法返回 405，Allow 头告诉客户端这里接受 POST。
    if (EVHTTP_REQ_POST != evhttp_request_get_command(req))
    {
        evhttp_add_header(evhttp_request_get_output_headers(req), "Allow", "POST");
        evhttp_send_reply(req, HTTP_BADMETHOD, "Method Not Allowed", nullptr);
        return;
    }

    Service *service = static_cast<Service *>(arg);
    std::filesystem::path path;
    bool fileWriteStarted = false;

    try
    {
        // 第 2 步：读取 FileName、StorageType 请求头。先用原始指针接收 C 接口的返回值，检查字段是否存在。
        auto headers = evhttp_request_get_input_headers(req);
        const char *nameHeader = evhttp_find_header(headers, "FileName");
        const char *typeHeader = evhttp_find_header(headers, "StorageType");
        // nullptr 表示请求头中没有这个字段，必须在构造 std::string 前检查。
        if (!nameHeader || !typeHeader)
        {
            evhttp_send_reply(req, HTTP_BADREQUEST, "Missing FileName or StorageType", nullptr);
            return;
        }

        // 指针有效后，再将 C 字符串内容复制到 std::string，供后面的业务逻辑使用。
        std::string encodedFileName = nameHeader, storageTypeText = typeHeader;
        // 字段存在也可能指向 ""，所以 FileName 还需检查空值；StorageType 的值由解析函数统一校验。
        if (encodedFileName.empty())
        {
            evhttp_send_reply(req, HTTP_BADREQUEST, "Empty FileName", nullptr);
            return;
        }

        // 解析函数负责值是否合法以及转换；Upload 捕获参数异常，转换成 HTTP 400 响应。
        StorageType storageType;
        try
        {
            storageType = parseStorageType(storageTypeText);
        }
        catch (...)
        {
            evhttp_send_reply(req, HTTP_BADREQUEST, "Illegal storage type", nullptr);
            return;
        }

        // 第 3 步：解码 UTF-8 文件名字节，例如 YS50eHQ= → a.txt；解码库报告错误时返回 400。
        // TODO 当前 cpp-base64 的解码规则较宽松：例如 "Zg" 缺少填充、"Zg=!" 含非法字符，也都能解码为 "f"。因此，解码成功只说明库能还原出字节，不代表输入符合标准 Base64 格式。若协议要求标准格式，严格校验可拒绝这类非规范输入，让客户端明确收到编码错误，而不是被宽松接受。这是编解码层的格式约束，适合统一实现并复用；Upload 负责调用解码及处理结果。后续在编解码工具层统一提供严格 Base64 校验。
        // TODO 后续统一补充文件名合法性校验：空名称、.、..、路径分隔符、控制字符、UTF-8 有效性及长度等。
        std::string fileName;
        try
        {
            fileName = base64_decode(encodedFileName);
        }
        catch (...)
        {
            evhttp_send_reply(req, HTTP_BADREQUEST, "Invalid Base64 fileName", nullptr);
            return;
        }

        // 第 4 步：请求头已解析为合法枚举，按枚举选择配置目录。
        std::filesystem::path directory;

        switch (storageType)
        {
            case StorageType::Uncompressed:
                directory = Config::GetInstance().getUncompressedFilesDirPath();
                break;
            case StorageType::Compressed:
                directory = Config::GetInstance().getCompressedFilesDirPath();
                break;
        }

        path = directory / fileName;

        // 第 5 步：先查内存中是否已经有对应 URL 的元数据，再查磁盘上是否已有文件；任意一处占用都返回 409。
        // 两种策略共用下载前缀和文件名，因此跨策略的同名文件也会冲突。
        // TODO 引入并发上传时补齐文件排他创建，避免检查后被其他写入占用。
        try
        {
            // getFileMetaDataByUrl() 函数未找到抛出异常，在 catch 分支被捕获然后正常执行后续，这正是我们想要的。如果找到，走 try 的后续分支返回 409。
            service->m_dataManager.getFileMetaDataByUrl(service->m_downloadPrefix + fileName);

            evhttp_send_reply(req, 409, "File already exists", nullptr);
            return;
        }
        catch (const std::out_of_range &)
        {
            // 未找到记录表示 URL 尚未使用，可以继续上传。
        }

        // symlink_status() 检查路径自身，连指向不存在目标的符号链接也视为已占用。
        if (std::filesystem::exists(std::filesystem::symlink_status(path)))
        {
            evhttp_send_reply(req, 409, "File already exists", nullptr);
            return;
        }

        // TODO 后续检查。

        // 第 6 步：通过前面的检查后，再分配内存并复制正文，保留二进制内容中的 '\0'。
        // TODO 在 HTTP 接收层和这里分配内存前统一限制上传大小。
        auto input = evhttp_request_get_input_buffer(req);
        auto length = evbuffer_get_length(input);
        std::string content(length, '\0');
        // copyout() 复制但不移除输入字节；空正文跳过复制，之后仍会创建零字节文件。
        if (length && evbuffer_copyout(input, content.data(), length) != static_cast<ev_ssize_t>(length)) throw std::runtime_error("Service::Upload(): evbuffer_copyout() failed");

        // 第 7 步：创建目录；只有压缩策略先处理内容，随后两种策略共用 setContent() 写入。
        // bundle 可能对不适合压缩的内容保留原字节，因此 compressed 不保证文件更小。
        utils::FileUtil(directory).createDirectory();
        if (StorageType::Compressed == storageType) content = utils::FileUtil::Compress(content, Config::GetInstance().getBundleFormat());
        // 写入前设置标记，写到一半失败时也能清理残留文件。
        fileWriteStarted = true;
        utils::FileUtil(path).setContent(content);

        // 第 8 步：从实际磁盘文件生成元数据，记录存储路径、修改时间和实际存储大小。
        // 先准备 JSON，避免序列化失败时已经提交元数据；对外下载 URL 进行百分号编码。
        const auto info = FileMetadata::FromFile(path);
        Json::Value result;
        result["url"] = utils::urlEncode(info.m_url);
        const auto body = utils::JsonUtil::Serialize(result);
        // insert() 新增内存记录并保存元数据 JSON；保存失败时撤销新增，再抛异常进入下面的 catch。
        service->m_dataManager.insert(info);

        // 第 9 步：文件和元数据全部保存成功后，用请求自带的输出缓冲区返回 201 和 JSON。
        auto *output = evhttp_request_get_output_buffer(req);
        evbuffer_add(output, body.data(), body.size());
        evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", "application/json;charset=utf-8");
        evhttp_send_reply(req, 201, "Created", nullptr);
    }
    catch (const std::exception &)
    {
        // TODO: 接入日志后记录原始异常，并报告文件清理错误。
        // 只清理本次已经开始写入的文件；error_code 重载避免清理失败再次抛异常。
        if (fileWriteStarted)
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
        evhttp_send_reply(req, HTTP_INTERNAL, "Upload failed", nullptr);
    }
}

void Service::ShowFileList(evhttp_request *req, void *arg)
{
    // arg 是注册回调时传入的 this，通过它访问当前服务的元数据管理器。
    Service *service = static_cast<Service *>(arg);
    auto files = service->m_dataManager.getAll();

    // 读取可执行文件旁 web 目录中的页面模板的内容。
    std::string content = utils::FileUtil(utils::executableDirectory() / "web" / "template.html").getContent();

    // 先替换服务器地址，避免把随后插入的文件名中的同名文本再次当作占位符。
    content = std::regex_replace(content, std::regex("\\{\\{BACKEND_URL\\}\\}"), "http://" + Config::GetInstance().getServerIp() + ":" + std::to_string(Config::GetInstance().getServerPort()));

    // regex_replace() 用 $$ 表示字面的 $，防止文件列表中的 $& 等内容被解释成替换指令。
    std::string fileListHtml;
    for (const char ch : GenerateFileListHtml(files))
    {
        if ('$' == ch) fileListHtml += '$';

        fileListHtml += ch;
    }
    content = std::regex_replace(content, std::regex("\\{\\{FILE_LIST\\}\\}"), fileListHtml);

    // 写入请求自带的响应缓冲区，设置 HTML 类型并返回 200。
    evbuffer *buf = evhttp_request_get_output_buffer(req);
    evbuffer_add(buf, content.data(), content.size());
    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", "text/html;charset=utf-8");
    evhttp_send_reply(req, HTTP_OK, nullptr, nullptr);
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
               << "<span class='file-type'>" << (isCompressed ? "压缩存储" : "未压缩存储") << "</span>"
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
