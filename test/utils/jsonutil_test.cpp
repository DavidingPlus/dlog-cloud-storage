#include <gtest/gtest.h>

#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "jsonutil.h"


namespace
{

    using utils::JsonUtil;

} // namespace


// JSON 值与文本转换。

TEST(JsonUtilTests, SerializeProducesJsonReadableByJsonCpp)
{
    Json::Value value;
    value["name"] = "report.txt";
    value["size"] = Json::UInt64(4096);
    value["ready"] = true;
    value["metadata"]["owner"] = "alice";
    value["tags"].append("documents");
    value["tags"].append("backup");

    const auto document = JsonUtil::Serialize(value);
    Json::CharReaderBuilder builder;
    std::istringstream stream(document);
    Json::Value restored;
    std::string errors;

    ASSERT_TRUE(Json::parseFromStream(builder, stream, &restored, &errors)) << errors;
    ASSERT_TRUE(restored.isObject());
    EXPECT_EQ(restored.size(), value.size());
    EXPECT_EQ(restored["name"].asString(), "report.txt");
    // JSON 文本不区分有符号和无符号整数，读回后按数值比较。
    EXPECT_EQ(restored["size"].asUInt64(), 4096U);
    EXPECT_TRUE(restored["ready"].asBool());
    EXPECT_EQ(restored["metadata"], value["metadata"]);
    EXPECT_EQ(restored["tags"], value["tags"]);
}

TEST(JsonUtilTests, SerializeEmitsUtf8KeysAndValues)
{
    Json::Value value;
    value[u8"文件名"] = u8"云存储/报告📦.txt";

    const auto document = JsonUtil::Serialize(value);

    EXPECT_NE(document.find(u8"文件名"), std::string::npos);
    EXPECT_NE(document.find(u8"云存储/报告📦.txt"), std::string::npos);
    EXPECT_EQ(JsonUtil::UnSerialize(document), value);
}

TEST(JsonUtilTests, SerializeEscapesControlCharactersAndPreservesTheirValues)
{
    Json::Value value;
    const std::string text("a\0b\n\r\t\"\\", 8);
    value["text"] = text;

    const auto document = JsonUtil::Serialize(value);

    EXPECT_EQ(document.find('\0'), std::string::npos);
    for (const auto *escape : {"\\u0000", "\\n", "\\r", "\\t", "\\\"", "\\\\"})
    {
        SCOPED_TRACE(escape);
        EXPECT_NE(document.find(escape), std::string::npos);
    }
    EXPECT_EQ(JsonUtil::UnSerialize(document)["text"].asString(), text);
}

TEST(JsonUtilTests, SerializeHandlesScalarRootsAndEmptyContainers)
{
    const struct
    {
        Json::Value value;
        std::string expected;
    } cases[] = {
        {Json::Value(), "null"}, {Json::Value(true), "true"}, {Json::Value(false), "false"}, {Json::Value(-42), "-42"}, {Json::Value(1.25), "1.25"}, {Json::Value("text"), R"("text")"}, {Json::Value(Json::arrayValue), "[]"}, {Json::Value(Json::objectValue), "{}"}};

    for (const auto &item : cases)
    {
        SCOPED_TRACE(item.expected);
        EXPECT_EQ(JsonUtil::Serialize(item.value), item.expected);
        EXPECT_EQ(JsonUtil::UnSerialize(item.expected), item.value);
    }
}

TEST(JsonUtilTests, UnSerializeReadsNestedStorageMetadata)
{
    const auto value = JsonUtil::UnSerialize(R"({
        "files": [{"name":"report.txt","size":4096}, {"name":"empty.txt","size":0}],
        "compressed": true,
        "ratio": 0.75,
        "checksum": null,
        "metadata": {"owner":"alice","version":2}
    })");

    ASSERT_TRUE(value.isObject());
    ASSERT_TRUE(value["files"].isArray());
    ASSERT_EQ(value["files"].size(), 2U);
    EXPECT_EQ(value["files"][0]["name"].asString(), "report.txt");
    EXPECT_EQ(value["files"][0]["size"].asUInt64(), 4096U);
    EXPECT_EQ(value["files"][1]["size"].asUInt64(), 0U);
    EXPECT_TRUE(value["compressed"].asBool());
    EXPECT_DOUBLE_EQ(value["ratio"].asDouble(), 0.75);
    EXPECT_TRUE(value["checksum"].isNull());
    EXPECT_EQ(value["metadata"]["owner"].asString(), "alice");
    EXPECT_EQ(value["metadata"]["version"].asInt(), 2);
}

TEST(JsonUtilTests, UnSerializeAcceptsWhitespaceAroundTheDocument)
{
    EXPECT_TRUE(JsonUtil::UnSerialize(" \t\r\n {} \n ").isObject());
    EXPECT_TRUE(JsonUtil::UnSerialize(" \n [] \t ").isArray());
    EXPECT_TRUE(JsonUtil::UnSerialize(" \n null \t ").isNull());
    EXPECT_EQ(JsonUtil::UnSerialize(" \n 42 \t ").asInt(), 42);
}

TEST(JsonUtilTests, UnSerializeDecodesUnicodeEscapesAndSurrogatePairs)
{
    const auto value = JsonUtil::UnSerialize(R"({"text":"\u4e91\u5b58\u50a8\ud83d\udce6"})");

    EXPECT_EQ(value["text"].asString(), u8"云存储📦");
}

TEST(JsonUtilTests, UnSerializeDecodesEscapedNulsAndOtherControlCharacters)
{
    const auto value = JsonUtil::UnSerialize(R"({"text":"\u0000\"\\\/\b\f\n\r\t"})");

    EXPECT_EQ(value["text"].asString(), std::string("\0\"\\/\b\f\n\r\t", 9));
}

TEST(JsonUtilTests, Preserves64BitIntegerBoundariesAndDoublePrecision)
{
    const auto value = JsonUtil::UnSerialize("[-9223372036854775808,9223372036854775807,18446744073709551615,1.2345678901234567]");

    ASSERT_EQ(value.size(), 4U);
    EXPECT_EQ(value[0].asInt64(), std::numeric_limits<Json::Int64>::min());
    EXPECT_EQ(value[1].asInt64(), std::numeric_limits<Json::Int64>::max());
    EXPECT_EQ(value[2].asUInt64(), std::numeric_limits<Json::UInt64>::max());
    EXPECT_DOUBLE_EQ(value[3].asDouble(), 1.2345678901234567);
    EXPECT_EQ(JsonUtil::UnSerialize(JsonUtil::Serialize(value)), value);
}

TEST(JsonUtilTests, UnSerializeReadsOnlyTheSelectedStringView)
{
    const std::string backing = R"(PREFIX{"id":7,"name":"report.txt"}SUFFIX)";
    const auto document = std::string_view(backing).substr(6, backing.size() - 12);

    const auto value = JsonUtil::UnSerialize(document);

    EXPECT_EQ(value["id"].asInt(), 7);
    EXPECT_EQ(value["name"].asString(), "report.txt");
}

TEST(JsonUtilTests, UnSerializeDoesNotReadPastTheEndOfATruncatedView)
{
    const std::string complete = R"({"ok":true})";
    const auto truncated = std::string_view(complete).substr(0, complete.size() - 1);

    // 底层字符串是合法 JSON，但传入的视图缺少结尾的 '}'，不能偷偷读取视图之外的字节。
    EXPECT_THROW(JsonUtil::UnSerialize(truncated), std::invalid_argument);
}

TEST(JsonUtilTests, UnSerializeRejectsEmptyAndMalformedDocuments)
{
    const std::string_view invalid[] = {
        {}, "", " \t\n", "{", "[", R"({"key" 1})", R"({"key":})", R"({"key":1 "other":2})", "[1 2]", "{unquoted:1}", R"({"key":"unterminated})", R"({"key":"\q"})", R"({"key":"\u12xz"})", "[truee]", "[1e]", "[1.2.3]"};

    for (const auto document : invalid)
    {
        SCOPED_TRACE(std::string(document));
        EXPECT_THROW(JsonUtil::UnSerialize(document), std::invalid_argument);
    }
}

TEST(JsonUtilTests, ParseFailuresIncludeTheJsonCppDiagnostic)
{
    try
    {
        JsonUtil::UnSerialize("{\n  \"name\":\n}");
        FAIL() << "Expected a parsing error";
    }
    catch (const std::invalid_argument &error)
    {
        const std::string prefix = "JsonUtil::UnSerialize(): ";
        const std::string message = error.what();
        EXPECT_EQ(message.find(prefix), 0U);
        EXPECT_GT(message.size(), prefix.size());
    }
}

TEST(JsonUtilTests, UsesJsonCppDefaultCommentAndTrailingCommaRules)
{
    // 工具类沿用库的默认配置；第三方库测试中的严格模式没有应用到这个接口。
    const auto value = JsonUtil::UnSerialize(R"({"id":/* metadata */7,})");

    EXPECT_EQ(value["id"].asInt(), 7);
}

TEST(JsonUtilTests, AFailedParseDoesNotAffectLaterCalls)
{
    EXPECT_EQ(JsonUtil::UnSerialize(R"({"id":1})")["id"].asInt(), 1);
    EXPECT_THROW(JsonUtil::UnSerialize("{"), std::invalid_argument);
    EXPECT_EQ(JsonUtil::UnSerialize(R"({"id":2})")["id"].asInt(), 2);
}

TEST(JsonUtilTests, RoundTripsALargeStorageMetadataCollection)
{
    Json::Value metadata;
    metadata["files"] = Json::Value(Json::arrayValue);
    for (Json::ArrayIndex i = 0; i < 512; ++i)
    {
        Json::Value file;
        file["name"] = u8"文件-" + std::to_string(i) + ".txt";
        file["size"] = Json::UInt64(i) * 4096;
        file["compressed"] = i % 2 == 0;
        metadata["files"].append(file);
    }

    const auto restored = JsonUtil::UnSerialize(JsonUtil::Serialize(metadata));
    ASSERT_TRUE(restored["files"].isArray());
    ASSERT_EQ(restored["files"].size(), metadata["files"].size());
    for (Json::ArrayIndex i = 0; i < metadata["files"].size(); ++i)
    {
        SCOPED_TRACE(i);
        const auto &expected = metadata["files"][i];
        const auto &actual = restored["files"][i];
        EXPECT_EQ(actual["name"].asString(), expected["name"].asString());
        EXPECT_EQ(actual["size"].asUInt64(), expected["size"].asUInt64());
        EXPECT_EQ(actual["compressed"].asBool(), expected["compressed"].asBool());
    }
}
