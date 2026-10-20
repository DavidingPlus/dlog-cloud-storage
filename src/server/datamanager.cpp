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
    auto absoluteFilePath = utils::executableDirectory() / filePath;
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
    // 构造函数中 m_metadataFilePath 已经被统一为绝对路径了，这里无需处理。下面 persistToDisk() 同理。
    utils::FileUtil file(m_metadataFilePath);
    if (!file.exists()) throw std::filesystem::filesystem_error("DataManager::loadFromDisk(): file does not exist", m_metadataFilePath, std::make_error_code(std::errc::no_such_file_or_directory));

    // 反序列化。
    auto root = utils::JsonUtil::UnSerialize(file.getContent());
    if (!root.isArray()) throw std::invalid_argument("DataManager::loadFromDisk(): root must be an array");

    // 将反序列化得到的 Json::Value 中的数据添加到 m_table 中。
    for (auto &item : root)
    {
        if (!item.isObject() || !item["mtime"].isInt64() || !item["file_size"].isUInt64() || !item["absolute_file_path"].isString() || !item["url"].isString()) throw std::invalid_argument("DataManager::loadFromDisk(): invalid storage info");

        FileMetadata info;

        // 与 persistToDisk() 对应，按文件系统时钟的原始计数还原时间。
        info.m_mtime = std::filesystem::file_time_type(std::filesystem::file_time_type::duration(item["mtime"].asInt64()));
        info.m_fileSize = static_cast<size_t>(item["file_size"].asUInt64());
        info.m_absoluteFilePath = item["absolute_file_path"].asString();
        info.m_url = item["url"].asString();

        insertWithoutPersist(info);
    }
}

void DataManager::persistToDisk()
{
    auto infos = getAll();
    Json::Value root(Json::arrayValue);

    for (auto &info : infos)
    {
        Json::Value item;

        // 保存文件系统时钟的原始计数，加载时使用相同的 duration 类型还原。
        item["mtime"] = static_cast<Json::Int64>(info.m_mtime.time_since_epoch().count());
        item["file_size"] = static_cast<Json::UInt64>(info.m_fileSize);
        item["absolute_file_path"] = info.m_absoluteFilePath.string();
        item["url"] = info.m_url;

        root.append(item);
    }

    auto body = utils::JsonUtil::Serialize(root);
    utils::FileUtil(m_metadataFilePath).setContent(body);
}

void DataManager::insert(const FileMetadata &info)
{
    insertWithoutPersist(info);
    persistToDisk();
}

void DataManager::update(const FileMetadata &info)
{
    {
        std::unique_lock lock(m_rwlock);

        auto it = m_table.find(info.m_url);
        if (m_table.end() == it) throw std::out_of_range("DataManager::update(): URL not found: " + info.m_url);

        it->second = info;
    }

    persistToDisk();
}

FileMetadata DataManager::getOneByUrl(const std::string &url) const
{
    std::shared_lock lock(m_rwlock);

    auto it = m_table.find(url);
    if (m_table.end() == it) throw std::out_of_range("DataManager::getOneByUrl(): URL not found: " + url);


    return it->second;
}

FileMetadata DataManager::getOneByFilePath(const std::filesystem::path &filePath) const
{
    std::shared_lock lock(m_rwlock);

    for (auto &entry : m_table)
    {
        if (filePath == entry.second.m_absoluteFilePath) return entry.second;
    }

    throw std::out_of_range("DataManager::getOneByFilePath(): file path not found: " + filePath.string());
}

std::vector<FileMetadata> DataManager::getAll() const
{
    std::shared_lock lock(m_rwlock);

    std::vector<FileMetadata> infos;
    infos.reserve(m_table.size());
    for (auto &entry : m_table) infos.push_back(entry.second);


    return infos;
}

void DataManager::insertWithoutPersist(const FileMetadata &info)
{
    std::unique_lock lock(m_rwlock);

    if (m_table.end() != m_table.find(info.m_url)) throw std::invalid_argument("DataManager::insertWithoutPersist(): URL already exists: " + info.m_url);

    m_table[info.m_url] = info;
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
