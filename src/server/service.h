#ifndef _DLOG_CLOUD_STORAGE_SERVER_SERVICE_H_
#define _DLOG_CLOUD_STORAGE_SERVER_SERVICE_H_

#include "globalmacros.h"

#include "server/datamanager.h"

#include <filesystem>
#include <string>
#include <vector>


struct evhttp_request;


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


// 文件存储类型枚举。
enum class StorageType
{
    Uncompressed,
    Compressed
};


// 基于 libevent 的 HTTP 文件存储服务。
class Service
{

public:

    // 读取服务配置并构造元数据管理器；元数据文件须提前准备，加载失败时抛出异常。
    Service();

    // 启动 HTTP 监听并阻塞运行事件循环。创建资源、绑定监听或运行事件循环失败时抛出 std::runtime_error。
    void run();


private:

    // libevent 通用请求回调，解析并解码请求路径，按参考流程分发请求。
    static void GenericCallback(evhttp_request *req, void *arg);

    // 上传处理：解码 FileName，按 StorageType 直接保存或压缩后保存，并持久化元数据，成功返回 201 和 JSON。允许上传零字节文件：正文长度为 0 时跳过字节复制，仍创建空文件并保存元数据，不视为错误。文件创建、写入或元数据保存失败时返回 500，并尝试清理本次上传文件。
    static void Upload(evhttp_request *req, void *arg);

    // 查询元数据并返回包含文件列表的 HTML 页面。
    static void ShowFileList(evhttp_request *req, void *arg);

    // 下载处理：查询元数据，读取或解压后返回原始文件内容。
    static void Download(evhttp_request *req, void *arg);

    // 按服务端本地时区格式化为 YYYY-MM-DD HH:MM:SS，时钟转换在函数内部完成。
    static std::string FormatFileTime(std::filesystem::file_time_type t);

    // 将文件字节数按 1024 进位格式化为 B/KB/MB/GB，保留两位小数。
    static std::string FormatFileSize(uint64_t bytes);

    // 根据元数据生成文件列表 HTML。
    static std::string GenerateFileListHtml(const std::vector<FileMetadata> &files);

    // 根据文件元数据生成版本标识，用于缓存验证和断点续传。
    static std::string GetETag(const FileMetadata &info);


    // 服务监听地址。
    std::string m_serverIp;

    // 服务监听端口。
    int m_serverPort = 0;

    // 下载 URL 前缀。
    std::string m_downloadPrefix;

    // 服务拥有的元数据管理器，构造时从持久化文件加载元数据到内存中。
    DataManager m_dataManager;
};


DLOG_CLOUD_STORAGE_NAMESPACE_END


#endif
