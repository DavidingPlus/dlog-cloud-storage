#ifndef _DLOG_CLOUD_STORAGE_COMMON_H_
#define _DLOG_CLOUD_STORAGE_COMMON_H_

#include "globalmacros.h"

#include <string>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(utils)


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


DLOG_CLOUD_STORAGE_NAMESPACE_END


#endif
