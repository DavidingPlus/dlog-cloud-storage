#include "server/config.h"

#include "utils/common.h"
#include "utils/fileutil.h"
#include "utils/jsonutil.h"

#include <stdexcept>

#include <bundle.h>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


namespace
{
    constexpr const char *kConfigFile = "config/server.conf";
}


Config &Config::GetInstance()
{
    // 详见：https://github.com/DavidingPlus/dfiber/tree/master/snippet/Singleton
    static Config instance;
    return instance;
}

void Config::readConfig()
{
    auto executableDir = utils::executableDirectory();
    auto configFile = executableDir / kConfigFile;

    auto root = utils::JsonUtil::UnSerialize(utils::FileUtil(configFile).getContent());
    if (!root.isObject()) throw std::invalid_argument("Config::readConfig(): root must be an object");

    // 校验字段。
    for (const char *field : {"server_ip", "download_prefix", "compressed_files_dir", "uncompressed_files_dir", "metadata_file"})
    {
        if (!root[field].isString() || root[field].asString().empty()) throw std::invalid_argument(std::string("Config::readConfig(): '") + field + "' must be a non-empty string");
    }

    auto &port = root["server_port"];
    if (!port.isInt() || port.asInt() < 1 || port.asInt() > 65535) throw std::invalid_argument("Config::readConfig(): 'server_port' must be an integer between 1 and 65535");

    auto &format = root["bundle_format"];
    if (!format.isInt() || format.asInt() < bundle::RAW || format.asInt() > bundle::BZIP2) throw std::invalid_argument("Config::readConfig(): 'bundle_format' is not supported");

    auto downloadPrefix = root["download_prefix"].asString();
    if (downloadPrefix.front() != '/' || downloadPrefix.back() != '/') throw std::invalid_argument("Config::readConfig(): 'download_prefix' must start and end with '/'");

    // 校验全部通过后再赋值。
    m_serverIp = root["server_ip"].asString();
    m_serverPort = root["server_port"].asInt();
    m_downloadPrefix = downloadPrefix;
    // 配置中的相对路径以可执行文件所在目录为基准，绝对路径直接使用。
    m_compressedFilesDirPath = executableDir / root["compressed_files_dir"].asString();
    m_uncompressedFilesDirPath = executableDir / root["uncompressed_files_dir"].asString();
    m_metadataFilePath = executableDir / root["metadata_file"].asString();
    m_bundleFormat = root["bundle_format"].asInt();
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
