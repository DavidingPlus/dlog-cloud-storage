#include "server/datamanager.h"

#include "utils/common.h"
#include "utils/fileutil.h"
#include "utils/jsonutil.h"

#include <mutex>
#include <stdexcept>
#include <system_error>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


FileMetadata FileMetadata::FromFile(const std::filesystem::path &filePath)
{
    // XXX path 的 / 运算符规则：右侧为绝对路径时，直接使用右侧绝对路径；右侧为相对路径时，拼接到左侧的可执行文件目录形成新绝对路径。
    auto absoluteFilePath = (utils::executableDirectory() / filePath).lexically_normal();
    utils::FileUtil file(absoluteFilePath);
    if (!file.exists()) throw std::filesystem::filesystem_error("FileMetadata::FromFile(): file does not exist", absoluteFilePath, std::make_error_code(std::errc::no_such_file_or_directory));

    FileMetadata info;
    info.m_mtime = file.lastModifyTime();
    info.m_fileSize = file.fileSize();
    info.m_absoluteFilePath = absoluteFilePath;
    info.m_url = Config::GetInstance().getDownloadPrefix() + file.fileName().string();


    return info;
}

void DataManager::loadFromDisk()
{
    // Config 已将元数据持久化文件路径解析为绝对路径，这里无需处理。下面 persistToDisk() 同理。
    utils::FileUtil file(m_metadataFilePath);
    if (!file.exists()) throw std::filesystem::filesystem_error("DataManager::loadFromDisk(): file does not exist", m_metadataFilePath, std::make_error_code(std::errc::no_such_file_or_directory));

    // 反序列化。
    auto root = utils::JsonUtil::UnSerialize(file.getContent());
    if (!root.isArray()) throw std::invalid_argument("DataManager::loadFromDisk(): root must be an array");

    // 加锁后逐条恢复内存表，入表函数内部不再重复加锁，也不写回磁盘。
    std::unique_lock lock(m_rwlock);

    for (auto &item : root)
    {
        if (!item.isObject() || !item["mtime"].isInt64() || !item["file_size"].isUInt64() || !item["absolute_file_path"].isString() || !item["url"].isString()) throw std::invalid_argument("DataManager::loadFromDisk(): invalid storage info");

        FileMetadata info;

        // 与 persistToDisk() 对应，按文件系统时钟的原始计数还原时间。
        info.m_mtime = std::filesystem::file_time_type(std::filesystem::file_time_type::duration(item["mtime"].asInt64()));
        info.m_fileSize = static_cast<size_t>(item["file_size"].asUInt64());
        info.m_absoluteFilePath = item["absolute_file_path"].asString();
        info.m_url = item["url"].asString();

        insertWithoutPersistUnlocked(info);
    }
}

void DataManager::persistToDisk()
{
    std::unique_lock lock(m_rwlock);

    persistToDiskUnlocked();
}

void DataManager::persistToDiskUnlocked()
{
    Json::Value root(Json::arrayValue);

    for (auto &entry : m_table)
    {
        auto &info = entry.second;
        Json::Value item;

        // 保存文件系统时钟的原始计数，加载时使用相同的 duration 类型还原。
        item["mtime"] = static_cast<Json::Int64>(info.m_mtime.time_since_epoch().count());
        item["file_size"] = static_cast<Json::UInt64>(info.m_fileSize);
        item["absolute_file_path"] = info.m_absoluteFilePath.string();
        item["url"] = info.m_url;

        root.append(item);
    }

    auto body = utils::JsonUtil::Serialize(root);

    // 复制正式文件的路径，得到一个独立的 path 对象。例如 /data/metadata.json。然后在路径末尾追加 ".tmp" 后缀，此时 /data/metadata.json.tmp。
    auto temporaryPath = m_metadataFilePath;
    temporaryPath += ".tmp";

    // symlink_status() 获取路径自身的状态，exists() 判断它是否存在。这样即使 .tmp 是一个指向不存在文件的符号链接，也识别为已占用。如果已存在，就抛出异常。这一步放在 try 外面，后面的清理代码不会删除这个原本就存在的文件，因为这本就不是当前函数需要做的事情。
    if (std::filesystem::exists(std::filesystem::symlink_status(temporaryPath))) throw std::filesystem::filesystem_error("DataManager::persistToDiskUnlocked(): temporary path already exists", temporaryPath, std::make_error_code(std::errc::file_exists));

    try
    {
        utils::FileUtil(temporaryPath).setContent(body);
        // 让临时文件继承正式文件的权限。
        if (std::filesystem::exists(m_metadataFilePath)) std::filesystem::permissions(temporaryPath, std::filesystem::status(m_metadataFilePath).permissions());
        // 完整写入并关闭后，临时文件替换为正式文件，原临时文件消失。
        std::filesystem::rename(temporaryPath, m_metadataFilePath);
    }
    catch (...)
    {
        std::error_code error;
        // 保存失败时，清理临时文件并重新抛出异常。不影响正式文件。
        std::filesystem::remove(temporaryPath, error);
        // 上层函数统一处理或输出具体的 throw xxx 信息。这里原样重新 throw 当前异常即可，保留其类型和错误信息。
        throw;
    }
}

void DataManager::insert(const FileMetadata &info)
{
    std::unique_lock lock(m_rwlock);

    // 入表成功后才进入保存与回滚流程，重复 URL 直接抛出，不会误删旧记录。
    insertWithoutPersistUnlocked(info);

    try
    {
        persistToDiskUnlocked();
    }
    catch (...)
    {
        m_table.erase(info.m_url);
        throw;
    }
}

void DataManager::update(const FileMetadata &info)
{
    auto normalizedInfo = info;
    normalizedInfo.m_absoluteFilePath = info.m_absoluteFilePath.lexically_normal();

    std::unique_lock lock(m_rwlock);

    auto it = m_table.find(info.m_url);
    if (m_table.end() == it) throw std::out_of_range("DataManager::update(): URL not found: " + info.m_url);

    // 用 normalizedInfo 接住旧值，失败时直接交换回来，不复制整张表。
    std::swap(it->second, normalizedInfo);
    try
    {
        persistToDiskUnlocked();
    }
    catch (...)
    {
        std::swap(it->second, normalizedInfo);
        throw;
    }
}

FileMetadata DataManager::getFileMetaDataByUrl(const std::string &url) const
{
    std::shared_lock lock(m_rwlock);

    auto it = m_table.find(url);
    if (m_table.end() == it) throw std::out_of_range("DataManager::getFileMetaDataByUrl(): URL not found: " + url);


    return it->second;
}

FileMetadata DataManager::getFileMetaDataByFilePath(const std::filesystem::path &filePath) const
{
    auto absoluteFilePath = (utils::executableDirectory() / filePath).lexically_normal();
    std::shared_lock lock(m_rwlock);

    for (auto &entry : m_table)
    {
        if (absoluteFilePath == entry.second.m_absoluteFilePath) return entry.second;
    }

    throw std::out_of_range("DataManager::getFileMetaDataByFilePath(): file path not found: " + filePath.string());
}

std::vector<FileMetadata> DataManager::getAll() const
{
    std::shared_lock lock(m_rwlock);

    std::vector<FileMetadata> infos;
    infos.reserve(m_table.size());
    for (auto &entry : m_table) infos.push_back(entry.second);


    return infos;
}

void DataManager::insertWithoutPersistUnlocked(const FileMetadata &info)
{
    // 锁和持久化由调用方负责。
    auto normalizedInfo = info;
    normalizedInfo.m_absoluteFilePath = info.m_absoluteFilePath.lexically_normal();

    auto inserted = m_table.emplace(info.m_url, std::move(normalizedInfo));
    if (!inserted.second) throw std::invalid_argument("DataManager::insertWithoutPersistUnlocked(): URL already exists: " + info.m_url);
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
