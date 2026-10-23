#ifndef _DLOG_CLOUD_STORAGE_SERVER_SERVICE_H_
#define _DLOG_CLOUD_STORAGE_SERVER_SERVICE_H_

#include "globalmacros.h"

#include "server/datamanager.h"

#include <filesystem>
#include <string>
#include <vector>


struct evhttp_request;


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


// 上传请求指定的服务端存储策略，分别对应 uncompressed、compressed。
enum class StorageType
{
    Uncompressed,
    Compressed
};


// 基于 libevent 的 HTTP 服务；接口沿用参考实现，当前仅提供接口骨架。
class Service
{

public:

    // 读取服务监听地址、端口和下载前缀配置。
    Service();

    // 启动 HTTP 监听并运行事件循环。
    bool runModule();


private:

    // libevent 通用请求回调，解析请求路径并分发请求。
    static void GenHandler(evhttp_request *req, void *arg);

    // 上传处理：读取 FileName、StorageType 请求头和原始文件字节。
    static void Upload(evhttp_request *req, void *arg);

    // 按服务端本地时区格式化为 YYYY-MM-DD HH:MM:SS，时钟转换在函数内部完成。
    static std::string FormatFileTime(std::filesystem::file_time_type t);

    // 根据元数据生成文件列表 HTML。
    static std::string GenerateModernFileList(const std::vector<FileMetadata> &files);

    // 将文件字节数格式化为可读大小。
    static std::string FormatSize(std::uint64_t bytes);

    // 查询元数据并返回包含文件列表的 HTML 页面。
    static void ListShow(evhttp_request *req, void *arg);

    // 根据文件元数据生成 ETag。
    static std::string GetETag(const FileMetadata &info);

    // 下载处理：查询元数据，读取或解压后返回原始文件内容。
    static void Download(evhttp_request *req, void *arg);


    // 服务监听地址。
    std::string m_serverIp;

    // 服务监听端口。
    int m_serverPort = 0;

    // 下载 URL 前缀。
    std::string m_downloadPrefix;
};


DLOG_CLOUD_STORAGE_NAMESPACE_END


#endif
