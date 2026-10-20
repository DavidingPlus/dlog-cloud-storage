#ifndef _DLOG_CLOUD_STORAGE_SERVER_CONFIG_H_
#define _DLOG_CLOUD_STORAGE_SERVER_CONFIG_H_

#include "globalmacros.h"

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
    // XMake 流程会自动将项目根目录的 config/server.conf 拷贝到可执行文件的目录。
    void readConfig();

    std::string getServerIp() const { return m_serverIp; }

    int getServerPort() const { return m_serverPort; }

    std::string getDownloadPrefix() const { return m_downloadPrefix; }

    std::string getDeepStorageDir() const { return m_deepStorageDir; }

    std::string getLowStorageDir() const { return m_lowStorageDir; }

    std::string getStorageInfoFile() const { return m_storageInfo; }

    int getBundleFormat() const { return m_bundleFormat; }


private:

    Config();


    // 服务地址。
    std::string m_serverIp;

    // 服务端口。
    int m_serverPort = 0;

    // 下载 URL 前缀。
    std::string m_downloadPrefix;

    // 深度存储文件的存储目录。
    std::string m_deepStorageDir;

    // 浅度存储文件的存储目录。
    std::string m_lowStorageDir;

    // 文件元数据的持久化文件。
    std::string m_storageInfo;

    // bundle 压缩格式编号。
    int m_bundleFormat = 0;
};


DLOG_CLOUD_STORAGE_NAMESPACE_END


#endif
