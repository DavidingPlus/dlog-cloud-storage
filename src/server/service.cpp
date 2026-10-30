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

#include <utf8.h>


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

    bool isValidFileName(std::string_view fileName)
    {
        // 1. 文件名不能为空，也不能是特殊目录名。
        if (fileName.empty() || "." == fileName || ".." == fileName) return false;

        // 2. 限制文件名长度，按字节计算。
        if (fileName.size() > 255) return false;

        // 3. Linux 文件名不能包含 '/' 或 NUL。
        if (std::string_view::npos != fileName.find('/') || std::string_view::npos != fileName.find('\0')) return false;

        // 4. 要求文件名符合 UTF-8 编码。
        return utf8::is_valid(fileName.begin(), fileName.end());
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

void Service::GenericCallback(evhttp_request *req, void *arg) noexcept
{
    try
    {
        // evhttp_request_get_evhttp_uri() 取出已解析的 URI；path 不含查询参数。先判空，再构造字符串，具体见 docs/libevent-request-uri.md。
        auto uri = evhttp_request_get_evhttp_uri(req);
        const char *rawPath = uri ? evhttp_uri_get_path(uri) : nullptr;
        if (!rawPath)
        {
            evhttp_send_reply(req, HTTP_BADREQUEST, "Invalid request URI", nullptr);
            return;
        }

        // 只将路径解码的参数异常归为 400；业务处理抛出的异常由外层统一返回 500。
        std::string requestPath;
        try
        {
            requestPath = utils::urlDecode(rawPath);
        }
        catch (const std::invalid_argument &)
        {
            evhttp_send_reply(req, HTTP_BADREQUEST, "Invalid URL encoding", nullptr);
            return;
        }

        // run() 注册回调时传入 this 指针。
        Service *service = static_cast<Service *>(arg);

        // 下载请求：下载前缀必须位于路径开头，避免把路径中间包含前缀的未知路由分发到下载处理。例如：
        // GET /download/a.txt：进入下载处理。
        // GET /other/download/a.txt：作为未知路由返回 404。
        // POST /other/download/a.txt：作为未知路由返回 404。
        if (0 == requestPath.find(service->m_downloadPrefix))
        {
            Download(req, arg);
        }
        // 上传请求。
        else if ("/upload" == requestPath)
        {
            Upload(req, arg);
        }
        // 显示已存储文件列表，返回一个 HTML 页面给浏览器。
        else if ("/" == requestPath)
        {
            ShowFileList(req, arg);
        }
        // 404。
        else
        {
            evhttp_send_reply(req, HTTP_NOTFOUND, "Not Found", nullptr);
        }
    }
    catch (...)
    {
        // TODO 接入日志后记录原始异常；响应只返回通用错误，不暴露内部文件路径等细节。
        evhttp_send_reply(req, HTTP_INTERNAL, "Internal Server Error", nullptr);
    }
}

void Service::Upload(evhttp_request *req, void *arg)
{
    // 整体流程：校验上传参数 → 保存数据文件 → 生成元数据 → 登记内存表并保存元数据文件 → 返回成功。
    // 数据文件保存实际内容。元数据文件在哪里、占多少空间、何时修改以及如何下载。
    // 1. 检查 HTTP 方法，上传只接受 POST。
    // 2. 读取 FileName、StorageType 请求头，检查缺失或空值，并将存储策略解析为枚举。
    // 3. 将 FileName 从 Base64 解码，校验单个文件名、UTF-8 和长度。
    // 4. 根据存储策略选择配置目录，与文件名组合成最终数据文件路径。
    // 5. 检查下载 URL 和磁盘路径是否已被占用，发生冲突返回 409。
    // 6. 把请求正文中的原始文件字节复制到 content，保留二进制内容中的 '\0'，允许零字节文件。
    // 7. 确保存储目录存在，将原始内容或按 bundle_format 压缩后的内容写入磁盘数据文件。
    // 8. 数据文件写入成功后，再完成以下流程：
    //    a. 根据实际文件生成 FileMetadata，包含绝对路径、修改时间、实际存储大小和下载 URL。
    //    b. 准备含下载 URL 的 HTTP 响应 JSON，此时仅生成字符串，尚未发送给客户端。
    //    c. 调用 DataManager::insert()，把这条记录加入内存表，再将整张表保存到所有文件共用的磁盘元数据 JSON 文件。
    // 9. 数据文件和元数据都保存成功后，向客户端返回 201 和前面准备的响应 JSON。
    // 元数据持久化失败时，DataManager 撤销内存表中的新增记录，并保留原磁盘元数据 JSON。文件写入或元数据保存失败后，Upload 尝试删除本次已开始写入的数据文件，并返回 500。

    // 1. 检查 HTTP 方法，上传只接受 POST。其他方法返回 405，Allow 头告诉客户端这里接受 POST。
    if (EVHTTP_REQ_POST != evhttp_request_get_command(req))
    {
        evhttp_add_header(evhttp_request_get_output_headers(req), "Allow", "POST");
        evhttp_send_reply(req, HTTP_BADMETHOD, "Method Not Allowed", nullptr);
        return;
    }

    Service *service = static_cast<Service *>(arg);
    std::filesystem::path filePath;
    bool fileWriteStarted = false;

    try
    {
        // 2. 读取 FileName、StorageType 请求头。先用原始指针接收 C 接口的返回值，检查字段是否存在。
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

        // 3. 解码 UTF-8 文件名字节，例如 YS50eHQ= → a.txt；解码或文件名校验失败时返回 400。
        // TODO 当前 cpp-base64 的解码规则较宽松：例如 "Zg" 缺少填充、"Zg=!" 含非法字符，也都能解码为 "f"。因此，解码成功只说明库能还原出字节，不代表输入符合标准 Base64 格式。若协议要求标准格式，严格校验可拒绝这类非规范输入，让客户端明确收到编码错误，而不是被宽松接受。这是编解码层的格式约束，适合统一实现并复用；Upload 负责调用解码及处理结果。后续在编解码工具层统一提供严格 Base64 校验。
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

        // 校验 fileName 后才构造路径 path。
        if (!isValidFileName(fileName))
        {
            evhttp_send_reply(req, HTTP_BADREQUEST, "Invalid file name", nullptr);
            return;
        }

        // 4. 请求头已解析为合法枚举，按枚举选择配置目录。
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

        filePath = directory / fileName;

        // 5. 先查内存中是否已经有对应 URL 的元数据，再查磁盘上是否已有文件；任意一处占用都返回 409。
        // 两种策略共用下载前缀和文件名，因此跨策略的同名文件也会冲突。
        // TODO 引入并发上传时补齐文件排他创建，避免检查后被其他写入占用。
        try
        {
            // getFileMetaDataByRequestPath() 函数未找到抛出异常，在 catch 分支被捕获然后正常执行后续，这正是我们想要的。如果找到，走 try 的后续分支返回 409。
            service->m_dataManager.getFileMetaDataByRequestPath(service->m_downloadPrefix + fileName);

            evhttp_send_reply(req, 409, "File already exists", nullptr);
            return;
        }
        catch (const std::out_of_range &)
        {
            // 未找到记录表示 URL 尚未使用，可以继续上传。
        }

        // symlink_status() 检查路径自身，连指向不存在目标的符号链接也视为已占用。
        if (std::filesystem::exists(std::filesystem::symlink_status(filePath)))
        {
            evhttp_send_reply(req, 409, "File already exists", nullptr);
            return;
        }

        // 6. 通过前面的检查后，再分配内存并复制正文，保留二进制内容中的 '\0'。
        // TODO 在 HTTP 接收层和这里分配内存前统一限制上传大小。

        // 取得请求正文缓冲区。
        auto inputBuf = evhttp_request_get_input_buffer(req);
        // 取得请求正文的字节数。
        size_t length = evbuffer_get_length(inputBuf);

        // 准备接收内容的 content。
        std::string content(length, '\0');
        // 空 length 跳过复制，之后仍会创建零字节文件。
        if (length)
        {
            // evbuffer_copyout() 复制但不移除输入字节。
            if (static_cast<ev_ssize_t>(length) != evbuffer_copyout(inputBuf, content.data(), length)) throw std::runtime_error("Service::Upload(): evbuffer_copyout() failed");
        }

        // 7. 创建目录。压缩策略先处理内容，随后两种策略共用 setContent() 写入。

        // createDirectory() 使用 std::filesystem::create_directories()。目录不存在：创建目录，包括缺失的父目录。目录已存在：直接继续，不会清空目录或重新创建。
        utils::FileUtil(directory).createDirectory();

        // bundle 可能对不适合压缩的内容保留原字节，因此 compressed 不保证文件更小。
        if (StorageType::Compressed == storageType) content = utils::FileUtil::Compress(content, Config::GetInstance().getBundleFormat());

        // 写入前设置标记，保证写到一半失败时也能清理残留文件。
        fileWriteStarted = true;

        utils::FileUtil(filePath).setContent(content);

        // 8. 从实际磁盘文件生成元数据，记录存储路径、修改时间和实际存储大小，然后添加到内存表中。
        // 这里有两份用途不同的 JSON：body 是给客户端的响应，m_dataManager 持久化的 JSON 是服务端的文件元数据。

        // 从磁盘文件生成元数据。
        auto info = FileMetadata::FromFile(filePath);

        // 先准备成功响应的 JSON，避免序列化失败时已经提交元数据；对外下载 URL 进行百分号编码。
        Json::Value result;
        result["url"] = utils::urlEncode(info.m_url);
        auto body = utils::JsonUtil::Serialize(result);

        // insert() 新增内存记录并持久化元数据 JSON 文件。保存失败时撤销新增，再抛异常进入下面的 catch，这一步是由 insert() 保证的。
        service->m_dataManager.insert(info);

        // 9. 数据文件和元数据文件全部保存成功后，用请求自带的输出缓冲区返回 201 和 JSON。
        auto outputBuf = evhttp_request_get_output_buffer(req);
        evbuffer_add(outputBuf, body.data(), body.size());
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
            std::filesystem::remove(filePath, error);
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
    for (auto &ch : GenerateFileListHtml(files))
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

void Service::Download(evhttp_request *req, void *arg)
{
    // 整体流程：解析下载 URL → 查询内存中的元数据 → 读取数据文件 → 按存储策略解压 → 返回原始文件内容。
    // 上传负责写入数据文件和登记元数据；下载根据已有元数据找到文件，读取后直接响应，不新增元数据记录。
    // 1. 检查 HTTP 方法，当前完整文件下载只接受 GET。
    // 2. 提取 URI 路径并进行一次 URL 解码，得到元数据表中的请求路径键。
    // 3. 按已解码的请求路径查询 FileMetadata，取得实际存储路径；记录不存在时返回 404。
    // 4. 根据实际存储目录判断 StorageType，目录无法识别时返回 500；数据文件丢失时返回 404。
    // 5. 读取磁盘文件；普通存储直接使用原字节，压缩存储在内存中解压，空文件返回空正文。
    // 6. 设置二进制响应类型、下载文件名及 ETag。
    // 7. 将完整的原始文件字节写入响应缓冲区，返回 200；读取或解压抛出异常时返回 500。
    // 下载过程只读取数据文件，不创建解压临时文件，也不修改内存元数据表或磁盘元数据 JSON。

    // 1. 其他方法返回 405，Allow 头说明当前接受 GET。
    // TODO 后续补齐 HEAD 等方法的响应语义。
    if (EVHTTP_REQ_GET != evhttp_request_get_command(req))
    {
        evhttp_add_header(evhttp_request_get_output_headers(req), "Allow", "GET");
        evhttp_send_reply(req, HTTP_BADMETHOD, "Method Not Allowed", nullptr);
        return;
    }

    Service *service = static_cast<Service *>(arg);

    try
    {
        // 2. 从请求的原始编码路径解码得到 requestPath，查询参数不参与查找；加号仍表示加号。
        // GenericCallback 的解码结果是其局部变量，这里重新读取原始 URI，避免对已解码的结果再次解码。
        auto requestPath = utils::urlDecode(evhttp_uri_get_path(evhttp_request_get_evhttp_uri(req)));

        // 3. 元数据提供实际文件位置，不能直接把客户端请求路径当成磁盘路径。
        FileMetadata info;
        try
        {
            info = service->m_dataManager.getFileMetaDataByRequestPath(requestPath);
        }
        catch (const std::out_of_range &)
        {
            evhttp_send_reply(req, HTTP_NOTFOUND, "File not found", nullptr);
            return;
        }

        // 4. 按照目录判断存储类型，同时判断数据文件是否存在。
        auto filePath = info.m_absoluteFilePath;
        StorageType storageType;

        // 将配置目录与文件名组合后比较完整路径，兼容目录末尾带 / 的写法。
        if ((Config::GetInstance().getUncompressedFilesDirPath() / filePath.filename()).lexically_normal() == filePath)
        {
            storageType = StorageType::Uncompressed;
        }
        else if ((Config::GetInstance().getCompressedFilesDirPath() / filePath.filename()).lexically_normal() == filePath)
        {
            storageType = StorageType::Compressed;
        }
        else
        {
            // 两个存储目录都未匹配，无法确定存储类型，返回内部错误并结束处理。但在正常的运作流程中根本走不到这个分支，只做防御性编程。
            evhttp_send_reply(req, HTTP_INTERNAL, "Unknown storage directory", nullptr);
            return;
        }

        utils::FileUtil file(filePath);
        if (!file.exists())
        {
            evhttp_send_reply(req, HTTP_NOTFOUND, "File not found", nullptr);
            return;
        }

        // 5. 先读取磁盘上实际保存的字节，若是压缩存储，在内存中解压为原始内容。
        // TODO 后续在工具层补齐压缩数据损坏检测，并增加读取/解压后的大小限制。
        // TODO 大文件发送优化：未压缩文件可使用 evbuffer_add_file() 直接发送，避免完整读入 content，并正确管理文件描述符的所有权和发送生命周期；压缩文件评估流式解压及分块发送，降低内存占用。
        auto content = file.getContent();
        if (StorageType::Compressed == storageType) content = utils::FileUtil::UnCompress(content);

        // 6. 设置二进制响应类型、下载文件名及 ETag。
        // TODO 后续补齐外部文件变化时的元数据同步、缓存条件请求，以及 Range/If-Range 断点下载。
        auto etag = GetETag(info);
        // Content-Disposition 用于明确浏览器的下载行为和保存文件名，下载主流程本身不依赖此响应头。attachment 建议按附件下载；filename* 指定建议文件名，避免浏览器仅根据请求路径推断名称。UTF-8'' 声明文件名使用 UTF-8，语言标记留空；百分号编码支持中文、空格等字符。例如文件名 "a b.txt" 会生成 "attachment; filename*=UTF-8''a%20b.txt"。
        auto disposition = "attachment; filename*=UTF-8''" + utils::fileNameEncode(filePath.filename().string());
        auto headers = evhttp_request_get_output_headers(req);

        // 使用 ETag 提供文件版本标识。
        evhttp_add_header(headers, "ETag", etag.data());
        // 指定附件下载方式和建议文件名。
        evhttp_add_header(headers, "Content-Disposition", disposition.data());
        // 声明正文是通用二进制数据。
        evhttp_add_header(headers, "Content-Type", "application/octet-stream");

        // 7. 按长度发送二进制正文，保留 '\0'；libevent 会根据实际正文生成 Content-Length 响应字段。
        // TODO 当前统一返回完整正文和 200；Range 尚未实现，暂不声明 Accept-Ranges: bytes。
        auto outputBuf = evhttp_request_get_output_buffer(req);
        evbuffer_add(outputBuf, content.data(), content.size());
        evhttp_send_reply(req, HTTP_OK, "OK", nullptr);
    }
    catch (const std::exception &)
    {
        // TODO 接入日志后记录读取或解压异常，并补充并发删除、替换文件等场景的处理。
        evhttp_send_reply(req, HTTP_INTERNAL, "Download failed", nullptr);
    }
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

    auto compressedDir = Config::GetInstance().getCompressedFilesDirPath();
    for (auto &file : files)
    {
        auto filePath = file.m_absoluteFilePath.lexically_normal();
        // 按配置的实际目录判断存储类型，避免根据文件名中的 compressed 等字样误判。
        bool isCompressed = filePath == (compressedDir / filePath.filename()).lexically_normal();

        stream << "<div class='file-item'>"
               << "<div class='file-info'>"
               << "<span>📄" << utils::escapeHtmlText(filePath.filename().string()) << "</span>"
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
