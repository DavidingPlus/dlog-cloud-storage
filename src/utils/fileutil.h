#ifndef _DLOG_CLOUD_STORAGE_FILEUTIL_H_
#define _DLOG_CLOUD_STORAGE_FILEUTIL_H_

#include "globalmacros.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(utils)


// 文件工具类，使用 std::filesystem 和标准文件流实现。
class FileUtil
{

public:

    // 按值接收路径，再移动到成员中保存。
    // 1. 传入已有 path 变量时，先拷贝出参数，再把参数移动到 m_path，调用者的原变量保持不变。
    // 2. 传入临时 path 对象时，C++17 可直接构造参数，再移动到成员中，避免复制路径内容。
    // 也可以用 const path &，虽然传参时不拷贝，但保存到 m_path 时仍需拷贝一份。
    explicit FileUtil(std::filesystem::path path) : m_path(std::move(path)) {}

    // 获取普通文件的大小，单位为字节；空文件返回 0，查询失败时抛出 std::filesystem::filesystem_error。
    uintmax_t fileSize() const { return std::filesystem::file_size(m_path); }

    // std::filesystem 不提供查询最近访问时间的接口，需要额外适配平台 API 和时间类型，很麻烦。暂不实现此接口。
    // std::filesystem::file_time_type lastAccessTime() const;

    // 获取文件内容最近一次修改的时间；查询失败时抛出 std::filesystem::filesystem_error。
    // 返回文件系统时钟的时间点，不应直接当作 time_t 使用。
    std::filesystem::file_time_type lastModifyTime() const { return std::filesystem::last_write_time(m_path); }

    // 提取路径末尾的名称，例如 data/report.txt -> report.txt。不访问磁盘，不关心文件存不存在。
    // 路径以目录分隔符结尾时返回空路径，例如 data/ -> 空路径。
    std::filesystem::path fileName() const { return m_path.filename(); }

    // 从第 pos 个字节（从 0 计数）开始，读取 len 个字节。例如文件内容为 "abcdef"，getPosLen(2, 3) 返回 "cde"。按二进制读取，保留 '\0'；len 为 0 时返回空字符串，此时 pos 可以等于文件大小。
    // 范围越界抛出 std::out_of_range，超出长度限制抛出 std::length_error。文件大小查询失败时抛出 std::filesystem::filesystem_error；打开、定位、读取或关闭失败时抛出 std::ios_base::failure。
    std::string getPosLen(uintmax_t pos, size_t len) const;

    // 从文件开头读取整个文件，复用 getPosLen()；空文件返回空字符串，错误处理与 getPosLen() 相同。
    std::string getContent() const { return getPosLen(0, static_cast<size_t>(fileSize())); }

    // 将 content 中的全部字节覆盖写入文件，保留 '\0'；文件不存在时创建，content 为空时清空文件。
    // 超出文件流的长度限制时抛出 std::length_error；打开、写入或关闭失败时抛出 std::ios_base::failure。
    // 正常返回表示已将数据交给操作系统并关闭文件，不保证数据已落盘。
    void setContent(std::string_view content) const;

    // 判断文件或目录是否存在；不存在时返回 false，其他查询错误抛出 std::filesystem::filesystem_error。
    // 符号链接按其目标是否存在判断。
    bool exists() const { return std::filesystem::exists(m_path); }

    // 创建目录，包括尚不存在的父目录；目录已存在时直接返回。路径已存在但不是目录，或创建失败时抛出 std::filesystem::filesystem_error。
    void createDirectory() const { std::filesystem::create_directories(m_path); }

    // 扫描路径所指目录中的普通文件，不递归进入子目录。扫描失败时抛出 std::filesystem::filesystem_error。
    std::vector<std::filesystem::path> scanDirectory() const;


    // 按 bundle 的 format 编号（例如 bundle::LZIP）压缩 content，返回压缩结果。
    // 空内容返回空字符串；压缩不生效时按 bundle 的约定返回原内容；非法编号抛出 std::invalid_argument。
    static std::string Compress(std::string_view content, int format);

    // 调用 bundle 解压 content，返回解压结果；空内容返回空字符串。
    static std::string UnCompress(std::string_view content);


private:

    std::filesystem::path m_path;
};


DLOG_CLOUD_STORAGE_NAMESPACE_END


#endif
