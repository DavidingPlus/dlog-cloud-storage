#ifndef _DLOG_CLOUD_STORAGE_SERVER_DATAMANAGER_H_
#define _DLOG_CLOUD_STORAGE_SERVER_DATAMANAGER_H_

#include "globalmacros.h"

#include "server/config.h"
#include "utils/common.h"

#include <filesystem>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


// 单个文件的存储元数据。
struct FileMetadata
{

    // 根据传入的文件路径，生成该文件的元数据信息的结构体，相对路径以可执行文件所在目录为基准。
    static FileMetadata FromFile(const std::filesystem::path &storagePath);


    // 文件最近一次修改的时间。
    std::filesystem::file_time_type m_mtime;

    // 文件大小，单位为字节。
    size_t m_fileSize = 0;

    // 文件存储路径，相对路径以可执行文件所在目录为基准。
    std::filesystem::path m_filePath;

    // 下载请求 URL 中的资源路径。
    std::string m_url;
};


// 文件元数据的内存管理、持久化与启动恢复。
class DataManager
{

    DLOG_CLOUD_STORAGE_CLASS_NONCOPYABLE(DataManager)

public:

    DataManager() : m_metadataFilePath(utils::executableDirectory() / Config::GetInstance().getStorageInfoFile()) { loadFromDisk(); }

    ~DataManager() = default;

    // 从磁盘的持久化文件加载元数据到内存，文件不存在或加载失败时抛出异常。主要在程序初始化运行时使用。
    void loadFromDisk();

    // 把当前内存中的整张元数据表保存到磁盘，失败时抛出异常。
    void persistToDisk();

    // 按 URL 新增元数据并持久化，URL 已存在时抛出 std::invalid_argument。
    void insert(const FileMetadata &info);

    // 按 URL 更新已有元数据并持久化，URL 不存在时抛出 std::out_of_range。
    void update(const FileMetadata &info);

    // 根据下载 URL 获取元数据副本，未找到时抛出 std::out_of_range。
    FileMetadata getOneByUrl(const std::string &url) const;

    // 根据文件存储路径获取元数据副本，未找到时抛出 std::out_of_range。
    FileMetadata getOneByStoragePath(const std::filesystem::path &storagePath) const;

    // 获取全部元数据的副本。
    std::vector<FileMetadata> getAll() const;


private:

    // 仅向内存表新增元数据，URL 重复时抛出 std::invalid_argument，供启动加载使用。
    void insertWithoutPersist(const FileMetadata &info);


    // 元数据持久化文件的绝对路径。
    std::filesystem::path m_metadataFilePath;

    // 元数据表的读写锁，允许 const 查询函数加锁。
    mutable std::shared_mutex m_rwlock;

    // 以下载 URL 为键的元数据表。
    std::unordered_map<std::string, FileMetadata> m_table;
};


DLOG_CLOUD_STORAGE_NAMESPACE_END


#endif
