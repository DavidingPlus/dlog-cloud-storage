#ifndef _DLOG_CLOUD_STORAGE_SERVER_DATAMANAGER_H_
#define _DLOG_CLOUD_STORAGE_SERVER_DATAMANAGER_H_

#include "globalmacros.h"

#include <cstddef>
#include <filesystem>
#include <string>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


// 单个文件的存储元数据。
struct StorageInfo
{

    bool newStorageInfo(const std::string &storagePath);


    // 文件最近一次修改的时间。
    std::filesystem::file_time_type m_mtime;

    // 文件大小，单位为字节。
    std::size_t m_fileSize = 0;

    // 文件存储路径。
    std::string m_storagePath;

    // 下载请求 URL 中的资源路径。
    std::string m_url;
};


DLOG_CLOUD_STORAGE_NAMESPACE_END


#endif
