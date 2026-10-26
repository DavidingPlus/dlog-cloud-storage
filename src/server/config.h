#ifndef _DLOG_CLOUD_STORAGE_SERVER_CONFIG_H_
#define _DLOG_CLOUD_STORAGE_SERVER_CONFIG_H_

#include "globalmacros.h"

#include <filesystem>
#include <string>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


// 服务端运行配置类。
class Config
{

    DLOG_CLOUD_STORAGE_CLASS_NONCOPYABLE(Config)

public:

    // 获取单例，首次访问时构造并读取配置；构造失败时异常向调用方传播。
    static Config &GetInstance();

    // 从可执行文件所在目录的 config/server.conf 读取配置，仅在启动阶段调用。字段校验失败抛出 std::invalid_argument。
    // 三个文件系统路径均以可执行文件所在目录为基准解析为绝对路径，并做词法标准化。
    // XMake 流程会自动将 src/server/config/server.conf 拷贝到可执行文件目录下的 config/server.conf。web/template.html 同理。
    void readConfig();

    std::string getServerIp() const { return m_serverIp; }

    int getServerPort() const { return m_serverPort; }

    std::string getDownloadPrefix() const { return m_downloadPrefix; }

    std::filesystem::path getCompressedFilesDirPath() const { return m_compressedFilesDirPath; }

    std::filesystem::path getUncompressedFilesDirPath() const { return m_uncompressedFilesDirPath; }

    std::filesystem::path getMetadataFilePath() const { return m_metadataFilePath; }

    int getBundleFormat() const { return m_bundleFormat; }


private:

    Config() { readConfig(); }


    // 服务地址。
    std::string m_serverIp;

    // 服务端口。
    int m_serverPort = 0;

    // 下载 URL 前缀。
    std::string m_downloadPrefix;

    // 经服务端压缩后的文件所在目录的绝对路径。
    std::filesystem::path m_compressedFilesDirPath;

    // 未经服务端压缩的文件所在目录的绝对路径。
    std::filesystem::path m_uncompressedFilesDirPath;

    // 文件元数据持久化文件的绝对路径。
    std::filesystem::path m_metadataFilePath;

    // bundle 压缩格式编号。
    int m_bundleFormat = 0;
};


DLOG_CLOUD_STORAGE_NAMESPACE_END


#endif
