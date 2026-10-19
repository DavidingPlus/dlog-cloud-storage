#include "utils/jsonutil.h"

#include <sstream>
#include <stdexcept>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(utils)


std::string JsonUtil::Serialize(const Json::Value &value)
{
    Json::StreamWriterBuilder builder;
    builder["emitUTF8"] = true;

    return Json::writeString(builder, value);
}

Json::Value JsonUtil::UnSerialize(std::string_view content)
{
    Json::CharReaderBuilder builder;
    // XXX C++ 风格的两种流的区别：istringstream 从内存字符串读取，构造参数是文本内容；ifstream 从文件读取，构造参数或 open() 的参数是文件路径。二者都继承 std::istream，提供 read()、operator>>() 等读取操作，并维护读取位置和流状态。
    // parseFromStream() 接收 std::istream &，两种输入流都能传入，将 JSON 文本 content 包装成字符串流。
    std::istringstream stream{std::string(content)};
    Json::Value value;
    std::string errors;

    if (!Json::parseFromStream(builder, stream, &value, &errors)) throw std::invalid_argument("JsonUtil::UnSerialize(): " + errors);


    return value;
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
