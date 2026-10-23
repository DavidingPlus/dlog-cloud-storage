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
    // 一些路径接口对比，示例见 snippet/FilesystemPathTest。
    // 1. lexically_normal()：按词法规则化简 .、可消去的 .. 和多余分隔符，相对路径仍为相对路径。
    // 2. lexically_relative(base)：计算从 base 到当前路径的相对路径，无法计算时返回空路径。
    // 3. lexically_proximate(base)：同上，但无法计算时返回当前路径。
    // 三个 lexically_* 接口均不访问文件系统、不解析符号链接；后两个不会先标准化路径。
    // 4. std::filesystem::canonical(p)：得到解析符号链接后的规范绝对路径，要求路径存在。
    // 5. std::filesystem::weakly_canonical(p)：解析已有前缀中的符号链接，允许后缀不存在，再对结果做词法标准化。
    m_compressedFilesDirPath = (executableDir / root["compressed_files_dir"].asString()).lexically_normal();
    m_uncompressedFilesDirPath = (executableDir / root["uncompressed_files_dir"].asString()).lexically_normal();
    m_metadataFilePath = (executableDir / root["metadata_file"].asString()).lexically_normal();
    m_bundleFormat = root["bundle_format"].asInt();
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
