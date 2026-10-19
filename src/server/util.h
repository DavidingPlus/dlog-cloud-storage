#ifndef _DLOG_CLOUD_STORAGE_SERVER_UTIL_H_
#define _DLOG_CLOUD_STORAGE_SERVER_UTIL_H_

#include "globalmacros.h"

#include <string>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


// 将 0~15 转换为大写十六进制字符；超出范围时抛出 std::invalid_argument。
unsigned char toHex(unsigned char x);

// 将 0~9、A~F、a~f 转换为数值；非法字符抛出 std::invalid_argument。
unsigned char fromHex(unsigned char x);

// 解码一次 %HH，保留 '+'；非法或不完整的转义抛出 std::invalid_argument。
std::string urlDecode(const std::string &str);


DLOG_CLOUD_STORAGE_NAMESPACE_END


#endif
