#ifndef _DLOG_CLOUD_STORAGE_UTILS_JSONUTIL_H_
#define _DLOG_CLOUD_STORAGE_UTILS_JSONUTIL_H_

#include "globalmacros.h"

#include <string>
#include <string_view>

#include <json/json.h>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(utils)


// JSON 工具类，使用 JsonCpp 在 JSON 值和文本之间转换。
class JsonUtil
{

public:

    // 将 value 序列化为 JSON 文本，中文等 Unicode 字符按 UTF-8 输出。
    static std::string Serialize(const Json::Value &value);

    // 将 content 中的 JSON 文本解析为 Json::Value，解析规则沿用 JsonCpp 默认配置。JsonCpp 报告解析失败时抛出 std::invalid_argument，异常消息包含错误原因。
    static Json::Value UnSerialize(std::string_view content);
};


DLOG_CLOUD_STORAGE_NAMESPACE_END


#endif
