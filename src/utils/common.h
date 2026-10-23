#ifndef _DLOG_CLOUD_STORAGE_UTILS_COMMON_H_
#define _DLOG_CLOUD_STORAGE_UTILS_COMMON_H_

#include "globalmacros.h"

#include <string>
#include <filesystem>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(utils)


// 返回当前进程可执行文件的绝对路径，包含文件名，不受当前工作目录影响。获取失败时抛出 std::system_error（包括派生的 std::filesystem::filesystem_error）。
std::filesystem::path executableFullPath();

// 返回当前进程可执行文件所在目录的绝对路径。
// 普通函数直接定义在头文件中时，需要显式 inline，允许多个编译单元包含相同定义，避免链接时重复定义。类体内定义的成员函数会隐式 inline；成员函数若在头文件中的类外定义，也需要显式 inline。此处 inline 用于满足定义规则，不保证编译器会展开函数调用；函数定义放在单个 .cpp 中时通常不需要 inline。
inline std::filesystem::path executableDirectory() { return executableFullPath().parent_path(); }

// 将数值 0~15 转换为一位大写十六进制字符。例如 toHex(9) == '9'、toHex(10) == 'A'、toHex(15) == 'F'。
// 只转换一位，不会将整数 255 转换为字符串 "FF"；x 大于 15 时抛出 std::invalid_argument。
unsigned char toHex(unsigned char x);

// 将一位十六进制字符转换为 0~15 的数值，接受 0~9、A~F、a~f。例如 fromHex('A') 和 fromHex('a') 均返回数值 10，而非字符 'A' 的编码。
// 与 toHex() 的转换方向相反；非法字符（例如 'G'）抛出 std::invalid_argument。
unsigned char fromHex(unsigned char x);

// 百分号编码：保留 ASCII 字母、数字和 -_.~，其余字节编码为 %HH；preserveSlash 指定是否额外保留 '/'。
// 输入按原始字节处理，已有 '%' 也会编码；例如 "a%20b" -> "a%2520b"。
std::string percentEncode(const std::string &text, bool preserveSlash);

// 编码下载 URL 路径，保留目录分隔符 '/'。
inline std::string urlEncode(const std::string &url) { return percentEncode(url, true); }

// 编码单个文件名，斜杠 '/' 也编码为 %2F。
inline std::string fileNameEncode(const std::string &fileName) { return percentEncode(fileName, false); }

// 把 URL 中形如 "%20" 的编码还原为对应字节，例如 "hello%20world.txt" -> "hello world.txt"。
// URL 标准约定用 "%HH" 表示一个字节： '%' 标记编码的开始，HH 是两位十六进制数字的占位写法。一位十六进制数字表示 4 位二进制，两位表示 8 位（00 ~FF，即 0 ~255），正好表示一个字节。
// 从左往右读取：普通内容原样保留；遇到 '%'，将后两位十六进制数字合成一个字节。例如 %20 -> 2 * 16 + 0 = 32，对应 ASCII 中的空格。'%' 后不足两位或含非法十六进制字符时抛出 std::invalid_argument。
// 为什么特别强调加号？路径中的加号可以直接写成 '+'，也可以编码为 "%2B"；本函数会把这两种写法都还原为加号。例如 "a+b.txt" 和 "a%2Bb.txt" 都得到 "a+b.txt"。表单编码另有“'+' 表示空格”的规则；本函数按路径规则处理，输入中的 '+' 保持不变。
std::string urlDecode(const std::string &url);


DLOG_CLOUD_STORAGE_NAMESPACE_END


#endif
