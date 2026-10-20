#include "server/datamanager.h"

#include "server/config.h"
#include "utils/fileutil.h"

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

bool DataManager::storage()
{
}

bool DataManager::insert(const StorageInfo &)
{
}

bool DataManager::update(const StorageInfo &)
{
}

bool DataManager::getOneByUrl(const std::string &, StorageInfo *) const
{
}

bool DataManager::getOneByStoragePath(const std::string &, StorageInfo *) const
{
}

bool DataManager::getAll(std::vector<StorageInfo> *) const
{
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
