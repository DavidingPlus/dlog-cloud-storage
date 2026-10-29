#ifndef _DLOG_CLOUD_STORAGE_SERVER_DATAMANAGER_H_
#define _DLOG_CLOUD_STORAGE_SERVER_DATAMANAGER_H_

#include "globalmacros.h"

#include "server/config.h"

#include <filesystem>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


// 单个文件的存储元数据。
struct FileMetadata
{

    // 根据传入文件路径生成元数据，返回对象中的 m_absoluteFilePath 始终为经过词法标准化的绝对路径。相对路径以可执行文件所在目录为基准。
    static FileMetadata FromFile(const std::filesystem::path &filePath);


    // 文件最近一次修改的时间。
    std::filesystem::file_time_type m_mtime;

    // 文件大小，单位为字节。
    size_t m_fileSize = 0;

    // 文件的绝对路径。
    std::filesystem::path m_absoluteFilePath;

    // 下载请求 URL 中的资源路径。
    std::string m_url;
};


// 文件元数据的内存管理、持久化与启动恢复。
class DataManager
{

    DLOG_CLOUD_STORAGE_CLASS_NONCOPYABLE(DataManager)

public:

    DataManager() : m_metadataFilePath(Config::GetInstance().getMetadataFilePath()) { loadFromDisk(); }

    ~DataManager() = default;

    // 从磁盘的持久化文件加载元数据到内存，文件不存在或加载失败时抛出异常。主要在程序初始化运行时使用。
    void loadFromDisk();

    // 将整张表写入同目录临时文件后替换正式 JSON；失败时保留原文件并抛出异常。
    void persistToDisk();

    // 按 URL 新增并持久化，保存失败时撤销新增；路径做词法标准化，URL 已存在时抛出 std::invalid_argument。
    void insert(const FileMetadata &info);

    // 按 URL 更新并持久化，保存失败时恢复旧记录；路径做词法标准化，URL 不存在时抛出 std::out_of_range。
    void update(const FileMetadata &info);

    // 根据已解码的下载请求路径（例如 /download/a b.txt）获取元数据副本，未找到时抛出 std::out_of_range。
    FileMetadata getFileMetaDataByRequestPath(const std::string &requestPath) const;

    // 根据文件路径获取元数据副本，相对路径以可执行文件所在目录为基准。
    // 查询路径转为绝对路径并做词法标准化后，与记录中的标准化路径比较；不要求文件存在，不解析符号链接。未找到时抛出 std::out_of_range。
    FileMetadata getFileMetaDataByFilePath(const std::filesystem::path &filePath) const;

    // 获取全部元数据的副本。
    std::vector<FileMetadata> getAll() const;


private:

    // 调用方已持有写锁，避免 insert()/update() 保存时重复加锁。
    void persistToDiskUnlocked();

    // 调用方已持有写锁；标准化路径并向内存表新增记录，URL 重复时抛出 std::invalid_argument，不写磁盘。
    void insertWithoutPersistUnlocked(const FileMetadata &info);


    // 元数据持久化文件的绝对路径。
    std::filesystem::path m_metadataFilePath;

    // 元数据表的读写锁，允许 const 查询函数加锁。
    mutable std::shared_mutex m_rwlock;

    // 以已解码的下载请求路径（m_url）为键的元数据表。
    std::unordered_map<std::string, FileMetadata> m_table;
};


DLOG_CLOUD_STORAGE_NAMESPACE_END


#endif
