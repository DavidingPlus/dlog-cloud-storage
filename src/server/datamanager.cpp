#include "server/datamanager.h"

#include "server/config.h"
#include "utils/fileutil.h"
#include "utils/jsonutil.h"

#include <mutex>
#include <stdexcept>
#include <system_error>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


StorageInfo StorageInfo::fromFile(const std::string &storagePath)
{
    utils::FileUtil file(storagePath);
    if (!file.exists()) throw std::filesystem::filesystem_error("StorageInfo::fromFile(): file does not exist", storagePath, std::make_error_code(std::errc::no_such_file_or_directory));

    StorageInfo info;
    info.m_mtime = file.lastModifyTime();
    info.m_fileSize = file.fileSize();
    info.m_storagePath = storagePath;
    info.m_url = Config::GetInstance().getDownloadPrefix() + file.fileName().string();


    return info;
}

// TODO

DataManager::DataManager()
{
}

DataManager::~DataManager()
{
}

bool DataManager::initLoad()
{
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
        item["storage_path"] = info.m_storagePath;
        item["url"] = info.m_url;

        root.append(item);
    }

    auto body = utils::JsonUtil::Serialize(root);
    utils::FileUtil(m_storageFile).setContent(body);
}

void DataManager::insert(const StorageInfo &info)
{
    insertWithoutPersist(info);
    persistToDisk();
}

void DataManager::update(const StorageInfo &info)
{
    {
        std::unique_lock lock(m_rwlock);

        auto it = m_table.find(info.m_url);
        if (m_table.end() == it) throw std::out_of_range("DataManager::update(): URL not found: " + info.m_url);

        it->second = info;
    }

    persistToDisk();
}

StorageInfo DataManager::getOneByUrl(const std::string &url) const
{
    std::shared_lock lock(m_rwlock);

    auto it = m_table.find(url);
    if (m_table.end() == it) throw std::out_of_range("DataManager::getOneByUrl(): URL not found: " + url);


    return it->second;
}

StorageInfo DataManager::getOneByStoragePath(const std::string &storagePath) const
{
    std::shared_lock lock(m_rwlock);

    for (auto &entry : m_table)
    {
        if (storagePath == entry.second.m_storagePath) return entry.second;
    }

    throw std::out_of_range("DataManager::getOneByStoragePath(): storage path not found: " + storagePath);
}

std::vector<StorageInfo> DataManager::getAll() const
{
    std::shared_lock lock(m_rwlock);

    std::vector<StorageInfo> infos;
    infos.reserve(m_table.size());
    for (auto &entry : m_table) infos.push_back(entry.second);


    return infos;
}

void DataManager::insertWithoutPersist(const StorageInfo &info)
{
    std::unique_lock lock(m_rwlock);

    if (m_table.end() != m_table.find(info.m_url)) throw std::invalid_argument("DataManager::insertWithoutPersist(): URL already exists: " + info.m_url);

    m_table[info.m_url] = info;
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
