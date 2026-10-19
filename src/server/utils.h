#ifndef _DLOG_CLOUD_STORAGE_SERVER_UTILS_H_
#define _DLOG_CLOUD_STORAGE_SERVER_UTILS_H_

#include "globalmacros.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


// 将数值 0~15 转换为一位大写十六进制字符。例如 toHex(9) == '9'、toHex(10) == 'A'、toHex(15) == 'F'。
// 只转换一位，不会将整数 255 转换为字符串 "FF"；x 大于 15 时抛出 std::invalid_argument。
unsigned char toHex(unsigned char x);

// 将一位十六进制字符转换为 0~15 的数值，接受 0~9、A~F、a~f。例如 fromHex('A') 和 fromHex('a') 均返回数值 10，而非字符 'A' 的编码。
// 与 toHex() 的转换方向相反；非法字符（例如 'G'）抛出 std::invalid_argument。
unsigned char fromHex(unsigned char x);

// 把 URL 中形如 "%20" 的编码还原为对应字节，例如 "hello%20world.txt" -> "hello world.txt"。
// URL 标准约定用 "%HH" 表示一个字节： '%' 标记编码的开始，HH 是两位十六进制数字的占位写法。一位十六进制数字表示 4 位二进制，两位表示 8 位（00 ~FF，即 0 ~255），正好表示一个字节。
// 从左往右读取：普通内容原样保留；遇到 '%'，将后两位十六进制数字合成一个字节。例如 %20 -> 2 * 16 + 0 = 32，对应 ASCII 中的空格。'%' 后不足两位或含非法十六进制字符时抛出 std::invalid_argument。
// 为什么特别强调加号？路径中的加号可以直接写成 '+'，也可以编码为 "%2B"；本函数会把这两种写法都还原为加号。例如 "a+b.txt" 和 "a%2Bb.txt" 都得到 "a+b.txt"。表单编码另有“'+' 表示空格”的规则；本函数按路径规则处理，输入中的 '+' 保持不变。
std::string urlDecode(const std::string &str);


// 文件工具类，使用 std::filesystem 和标准文件流实现。
class FileUtil
{

public:

    // 按值接收路径，再移动到成员中保存。
    // 1. 传入已有 path 变量时，先拷贝出参数，再把参数移动到 m_path，调用者的原变量保持不变。
    // 2. 传入临时 path 对象时，C++17 可直接构造参数，再移动到成员中，避免复制路径内容。
    // 也可以用 const path &，虽然传参时不拷贝，但保存到 m_path 时仍需拷贝一份。
    explicit FileUtil(std::filesystem::path path) : m_path(std::move(path)) {}

    // 获取文件大小，单位为字节。
    uintmax_t fileSize() const;

    // 获取最近访问时间。C++17 的 std::filesystem 不提供查询接口，先保留占位，后续确定处理方式。
    std::filesystem::file_time_type lastAccessTime() const;

    // 获取最近修改时间。
    std::filesystem::file_time_type lastModifyTime() const;

    // 从路径中提取文件名，例如 data/report.txt -> report.txt。
    std::filesystem::path fileName() const;

    // 从 pos 开始读取 len 个字节。
    std::string getPosLen(uintmax_t pos, size_t len) const;

    // 读取整个文件的二进制内容。
    std::string getContent() const;

    // 将二进制内容覆盖写入文件。
    void setContent(std::string_view content) const;

    // 按 format 指定的格式压缩 content，将结果写入当前路径的文件。
    void compress(std::string_view content, int format) const;

    // 读取当前路径的压缩文件，解压后写入 downloadPath 指定的文件。
    void unCompress(const std::filesystem::path &downloadPath) const;

    // 判断文件或目录是否存在。
    bool exists() const;

    // 创建目录，包括尚不存在的父目录。
    void createDirectory() const;

    // 扫描当前目录中的普通文件，不递归进入子目录。
    std::vector<std::filesystem::path> scanDirectory() const;


private:

    std::filesystem::path m_path;
};


DLOG_CLOUD_STORAGE_NAMESPACE_END


#endif
