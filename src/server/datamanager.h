#ifndef _DLOG_CLOUD_STORAGE_SERVER_DATAMANAGER_H_
#define _DLOG_CLOUD_STORAGE_SERVER_DATAMANAGER_H_

#include "globalmacros.h"

#include <filesystem>
#include <string>


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


DLOG_CLOUD_STORAGE_NAMESPACE_END


#endif
