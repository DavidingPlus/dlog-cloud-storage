#ifndef _DLOG_CLOUD_STORAGE_SERVER_DATAMANAGER_H_
#define _DLOG_CLOUD_STORAGE_SERVER_DATAMANAGER_H_

#include "globalmacros.h"

#include <filesystem>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


// 单个文件的存储元数据。
struct StorageInfo
{

    // 根据传入的文件路径，生成该文件的元数据信息的结构体。
    static StorageInfo fromFile(const std::string &storagePath);


    // 文件最近一次修改的时间。
    std::filesystem::file_time_type m_mtime;

    // 文件大小，单位为字节。
    size_t m_fileSize = 0;

    // 文件存储路径。
    std::string m_storagePath;

    // 下载请求 URL 中的资源路径。
    std::string m_url;
};


// 文件元数据的内存管理、持久化与启动恢复。
class DataManager
{

    DLOG_CLOUD_STORAGE_CLASS_NONCOPYABLE(DataManager)

public:

    DataManager();

    ~DataManager();

    // 从持久化文件加载元数据。
    bool initLoad();

    // 将当前元数据表保存到持久化文件。
    bool storage();

    // 按 URL 插入元数据，相同 URL 覆盖原记录。
    bool insert(const StorageInfo &info);

    // 按 URL 更新元数据。
    bool update(const StorageInfo &info);

    // 根据下载 URL 获取元数据副本，未找到时抛出 std::out_of_range。
    StorageInfo getOneByUrl(const std::string &url) const;

    // 根据文件存储路径获取元数据副本，未找到时抛出 std::out_of_range。
    StorageInfo getOneByStoragePath(const std::string &storagePath) const;

    // 获取全部元数据的副本。
    std::vector<StorageInfo> getAll() const;


private:

    // 元数据持久化文件路径。
    std::string m_storageFile;

    // 元数据表的读写锁，允许 const 查询函数加锁。
    mutable std::shared_mutex m_rwlock;

    // 以下载 URL 为键的元数据表。
    std::unordered_map<std::string, StorageInfo> m_table;

    // 加载历史记录时关闭自动持久化。
    bool m_needPersist = false;
};


DLOG_CLOUD_STORAGE_NAMESPACE_END


#endif
