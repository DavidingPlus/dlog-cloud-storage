#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include <json/json.h>


namespace
{

    class JsonCppTests : public ::testing::Test
    {

    protected:

        JsonCppTests()
        {
            // 显式启用严格解析，并允许 JSON 标准中的标量根值。
            Json::CharReaderBuilder::strictMode(&reader_builder_.settings_);
            reader_builder_["strictRoot"] = false;
        }

        bool parse(const std::string &document)
        {
            root_ = Json::Value();
            errors_.clear();
            const std::unique_ptr<Json::CharReader> reader(reader_builder_.newCharReader());
            return reader->parse(document.data(), document.data() + document.size(), &root_, &errors_);
        }

        std::string write(const Json::Value &value, const std::string &indentation = "")
        {
            Json::StreamWriterBuilder builder;
            builder["commentStyle"] = "None";
            builder["indentation"] = indentation;
            return Json::writeString(builder, value);
        }

        Json::CharReaderBuilder reader_builder_;
        Json::Value root_;
        std::string errors_;
    };

} // namespace


TEST_F(JsonCppTests, ConstructsNullAndScalarValues)
{
    const Json::Value null;
    EXPECT_EQ(null.type(), Json::nullValue);
    EXPECT_TRUE(null.isNull());

    const Json::Value boolean(true);
    ASSERT_EQ(boolean.type(), Json::booleanValue);
    EXPECT_TRUE(boolean.asBool());

    const Json::Value integer(-42);
    ASSERT_EQ(integer.type(), Json::intValue);
    EXPECT_EQ(integer.asInt(), -42);

    const Json::Value unsigned_integer(Json::UInt(42));
    ASSERT_EQ(unsigned_integer.type(), Json::uintValue);
    EXPECT_EQ(unsigned_integer.asUInt(), 42U);

    const Json::Value real(1.25);
    ASSERT_EQ(real.type(), Json::realValue);
    EXPECT_DOUBLE_EQ(real.asDouble(), 1.25);

    const Json::Value string("cloud storage");
    ASSERT_EQ(string.type(), Json::stringValue);
    EXPECT_EQ(string.asString(), "cloud storage");
}

TEST_F(JsonCppTests, InsertsUpdatesAndRemovesObjectMembers)
{
    Json::Value object(Json::objectValue);
    object["name"] = "report.txt";
    object["size"] = 128;
    object["size"] = 256;

    ASSERT_TRUE(object.isObject());
    EXPECT_EQ(object.size(), 2U);
    EXPECT_EQ(object["size"].asInt(), 256);
    EXPECT_TRUE(object.isMember("name"));

    Json::Value removed;
    ASSERT_TRUE(object.removeMember("name", &removed));
    EXPECT_EQ(removed.asString(), "report.txt");
    EXPECT_FALSE(object.isMember("name"));
    EXPECT_FALSE(object.removeMember("missing", &removed));

    object.clear();
    EXPECT_TRUE(object.isObject());
    EXPECT_TRUE(object.empty());
}

TEST_F(JsonCppTests, MissingMembersAndDefaultsDoNotModifyConstObjects)
{
    Json::Value object(Json::objectValue);
    object["present"] = Json::Value();
    const Json::Value &view = object;

    EXPECT_TRUE(view.isMember("present"));
    EXPECT_TRUE(view["present"].isNull());
    EXPECT_FALSE(view.isMember("missing"));
    EXPECT_TRUE(view["missing"].isNull());
    EXPECT_EQ(view.get("missing", "fallback").asString(), "fallback");
    EXPECT_TRUE(view.get("present", "fallback").isNull());
    EXPECT_EQ(view.size(), 1U);
}

TEST_F(JsonCppTests, AppendsResizesAndRemovesArrayElements)
{
    Json::Value array(Json::arrayValue);
    array.append("first");
    array.append(2);
    array.append(true);

    Json::Value removed;
    ASSERT_TRUE(array.removeIndex(1, &removed));
    EXPECT_EQ(removed.asInt(), 2);
    ASSERT_EQ(array.size(), 2U);
    EXPECT_EQ(array[0].asString(), "first");
    EXPECT_TRUE(array[1].asBool());
    EXPECT_FALSE(array.removeIndex(10, &removed));

    array.resize(4);
    EXPECT_TRUE(array[2].isNull());
    EXPECT_TRUE(array[3].isNull());
    array.resize(1);
    ASSERT_EQ(array.size(), 1U);
    EXPECT_EQ(array[0].asString(), "first");

    array.clear();
    EXPECT_TRUE(array.isArray());
    EXPECT_TRUE(array.empty());
}

TEST_F(JsonCppTests, CopiesNestedValuesWithoutSharingMutableState)
{
    Json::Value original;
    original["files"][0]["name"] = "original.txt";
    original["files"][0]["size"] = 10;

    Json::Value copy = original;
    copy["files"][0]["name"] = "changed.txt";
    copy["files"].append(Json::Value(Json::objectValue));

    EXPECT_EQ(original["files"][0]["name"].asString(), "original.txt");
    EXPECT_EQ(original["files"].size(), 1U);
    EXPECT_EQ(copy["files"][0]["name"].asString(), "changed.txt");
    EXPECT_EQ(copy["files"][0]["size"].asInt(), 10);
    EXPECT_EQ(copy["files"].size(), 2U);
}

TEST_F(JsonCppTests, EnumeratesObjectMembersAndIteratesArrays)
{
    Json::Value object;
    object["id"] = 7;
    object["name"] = "file.txt";
    const auto names = object.getMemberNames();
    EXPECT_EQ((std::set<std::string>(names.begin(), names.end())),
              (std::set<std::string>{"id", "name"}));

    Json::Value array(Json::arrayValue);
    for (int value : {1, 2, 3, 4})
    {
        array.append(value);
    }

    int sum = 0;
    for (const auto &value : array)
    {
        sum += value.asInt();
    }
    EXPECT_EQ(sum, 10);
}

TEST_F(JsonCppTests, ExplicitStringRangePreservesEmbeddedNuls)
{
    const std::string input("a\0b\0c", 5);
    const Json::Value value(input.data(), input.data() + input.size());

    ASSERT_TRUE(value.isString());
    EXPECT_EQ(value.asString().size(), input.size());
    EXPECT_EQ(value.asString(), input);
}

TEST_F(JsonCppTests, InvalidTypeConversionsReportLogicErrors)
{
    const Json::Value text("42");
    const Json::Value object(Json::objectValue);

    EXPECT_THROW(text.asInt(), Json::LogicError);
    EXPECT_THROW(object.asString(), Json::LogicError);
    EXPECT_THROW(object.asBool(), Json::LogicError);
}

TEST_F(JsonCppTests, ParsesNestedStorageMetadataWithAllJsonTypes)
{
    const std::string document = R"({
        "name": "report.txt",
        "size": 4096,
        "compressed": false,
        "ratio": 0.75,
        "checksum": null,
        "tags": ["documents", "backup"],
        "metadata": {"owner": "alice", "version": 2}
    })";

    ASSERT_TRUE(parse(document)) << errors_;
    EXPECT_TRUE(errors_.empty());
    ASSERT_TRUE(root_.isObject());
    EXPECT_EQ(root_["name"].asString(), "report.txt");
    EXPECT_EQ(root_["size"].asUInt64(), 4096U);
    EXPECT_FALSE(root_["compressed"].asBool());
    EXPECT_DOUBLE_EQ(root_["ratio"].asDouble(), 0.75);
    EXPECT_TRUE(root_["checksum"].isNull());
    ASSERT_TRUE(root_["tags"].isArray());
    ASSERT_EQ(root_["tags"].size(), 2U);
    EXPECT_EQ(root_["tags"][1].asString(), "backup");
    EXPECT_EQ(root_["metadata"]["owner"].asString(), "alice");
    EXPECT_EQ(root_["metadata"]["version"].asInt(), 2);
}

TEST_F(JsonCppTests, ParsesEmptyContainersAndSurroundingWhitespace)
{
    ASSERT_TRUE(parse(" \t\r\n {} \n")) << errors_;
    EXPECT_TRUE(root_.isObject());
    EXPECT_TRUE(root_.empty());

    ASSERT_TRUE(parse("\n [] \r\n")) << errors_;
    EXPECT_TRUE(root_.isArray());
    EXPECT_TRUE(root_.empty());
}

TEST_F(JsonCppTests, ParsesScalarRootValues)
{
    const struct
    {
        std::string document;
        Json::Value expected;
    } cases[] = {
        {"null", Json::Value()},
        {"true", Json::Value(true)},
        {"false", Json::Value(false)},
        {"-42", Json::Value(-42)},
        {"1.25", Json::Value(1.25)},
        {R"("text")", Json::Value("text")}};

    for (const auto &test_case : cases)
    {
        SCOPED_TRACE(test_case.document);
        ASSERT_TRUE(parse(test_case.document)) << errors_;
        EXPECT_EQ(root_, test_case.expected);
    }
}

TEST_F(JsonCppTests, ParsesExactSignedAndUnsigned64BitBoundaries)
{
    ASSERT_TRUE(parse("[-9223372036854775808,9223372036854775807,18446744073709551615]")) << errors_;
    ASSERT_EQ(root_.size(), 3U);
    ASSERT_TRUE(root_[0].isInt64());
    EXPECT_EQ(root_[0].asInt64(), std::numeric_limits<Json::Int64>::min());
    ASSERT_TRUE(root_[1].isInt64());
    EXPECT_EQ(root_[1].asInt64(), std::numeric_limits<Json::Int64>::max());
    ASSERT_TRUE(root_[2].isUInt64());
    EXPECT_EQ(root_[2].asUInt64(), std::numeric_limits<Json::UInt64>::max());
    EXPECT_FALSE(root_[2].isInt64());
}

TEST_F(JsonCppTests, ParsesFractionalAndExponentNumbers)
{
    ASSERT_TRUE(parse("[-1.25,6.022e23,1E-6,0.0]")) << errors_;
    ASSERT_EQ(root_.size(), 4U);
    EXPECT_DOUBLE_EQ(root_[0].asDouble(), -1.25);
    EXPECT_DOUBLE_EQ(root_[1].asDouble(), 6.022e23);
    EXPECT_DOUBLE_EQ(root_[2].asDouble(), 1e-6);
    EXPECT_DOUBLE_EQ(root_[3].asDouble(), 0.0);
}

TEST_F(JsonCppTests, ParsesUtf8MemberNamesAndValues)
{
    ASSERT_TRUE(parse(u8R"({"文件名":"云存储/报告📦.txt"})")) << errors_;
    EXPECT_EQ(root_[u8"文件名"].asString(), u8"云存储/报告📦.txt");
}

TEST_F(JsonCppTests, DecodesUnicodeEscapesAndSurrogatePairs)
{
    ASSERT_TRUE(parse(R"({"text":"\u4e91\u5b58\u50a8\ud83d\udce6"})")) << errors_;
    EXPECT_EQ(root_["text"].asString(), u8"云存储📦");
}

TEST_F(JsonCppTests, DecodesEscapedCharactersAndEmbeddedNuls)
{
    ASSERT_TRUE(parse(R"({"text":"\u0000\"\\\/\b\f\n\r\t"})")) << errors_;
    EXPECT_EQ(root_["text"].asString(), std::string("\0\"\\/\b\f\n\r\t", 9));
}

TEST_F(JsonCppTests, CharReaderRespectsExplicitDocumentBoundaries)
{
    const std::string buffer = "prefix{\"id\":7}suffix";
    const char *begin = buffer.data() + 6;
    const char *end = buffer.data() + buffer.size() - 6;
    const std::unique_ptr<Json::CharReader> reader(reader_builder_.newCharReader());

    ASSERT_TRUE(reader->parse(begin, end, &root_, &errors_)) << errors_;
    EXPECT_EQ(root_["id"].asInt(), 7);
}

TEST_F(JsonCppTests, RejectsMalformedDocumentsWithDiagnostics)
{
    const std::string documents[] = {
        "", " \t\n", "{", "[", R"({"key" 1})", R"({"key":})",
        R"({"key":1 "other":2})", "[1 2]", "{unquoted:1}",
        R"({"key":"unterminated})", R"({"key":"\q"})", R"({"key":"\u12xz"})",
        R"(["\ud83d"])", "[truee]", "[1e]", "[1.2.3]"};

    for (const auto &document : documents)
    {
        SCOPED_TRACE(document);
        EXPECT_FALSE(parse(document));
        EXPECT_FALSE(errors_.empty());
    }
}

TEST_F(JsonCppTests, StrictModeRejectsNonstandardExtensions)
{
    const std::string documents[] = {
        R"({"id":/* comment */1})", R"({"id":1,})", "[1,]",
        "{'id':1}", "{1:2}", "[1,,2]", "[NaN]", "[Infinity]", "[-Infinity]"};

    for (const auto &document : documents)
    {
        SCOPED_TRACE(document);
        EXPECT_FALSE(parse(document));
        EXPECT_FALSE(errors_.empty());
    }
}

TEST_F(JsonCppTests, StrictModeRejectsDuplicateKeysAndTrailingContent)
{
    const std::string documents[] = {
        R"({"id":1,"id":2})", "{} []", "[] trailing", "true false"};

    for (const auto &document : documents)
    {
        SCOPED_TRACE(document);
        EXPECT_FALSE(parse(document));
        EXPECT_FALSE(errors_.empty());
    }
}

TEST_F(JsonCppTests, ReaderCanBeReusedAfterParseFailure)
{
    const std::unique_ptr<Json::CharReader> reader(reader_builder_.newCharReader());
    const std::string invalid = "{";
    EXPECT_FALSE(reader->parse(invalid.data(), invalid.data() + invalid.size(), &root_, &errors_));
    EXPECT_FALSE(errors_.empty());

    const std::string valid = R"({"ok":true})";
    ASSERT_TRUE(reader->parse(valid.data(), valid.data() + valid.size(), &root_, &errors_)) << errors_;
    EXPECT_TRUE(errors_.empty());
    EXPECT_TRUE(root_["ok"].asBool());
}

TEST_F(JsonCppTests, DepthLimitReportsExcessiveNesting)
{
    reader_builder_["stackLimit"] = 8;
    ASSERT_TRUE(parse("[[[0]]]")) << errors_;

    const std::string deeply_nested = std::string(32, '[') + "0" + std::string(32, ']');
    EXPECT_THROW(parse(deeply_nested), Json::RuntimeError);
}

TEST_F(JsonCppTests, ParsesStreamsAndReportsInvalidStreamInput)
{
    std::istringstream valid(R"({"files":["a.txt","b.txt"]})");
    ASSERT_TRUE(Json::parseFromStream(reader_builder_, valid, &root_, &errors_)) << errors_;
    ASSERT_EQ(root_["files"].size(), 2U);
    EXPECT_EQ(root_["files"][0].asString(), "a.txt");

    std::istringstream invalid(R"({"files":[})");
    EXPECT_FALSE(Json::parseFromStream(reader_builder_, invalid, &root_, &errors_));
    EXPECT_FALSE(errors_.empty());
}

TEST_F(JsonCppTests, WritesCompactJsonWithExpectedSyntax)
{
    Json::Value object;
    object["name"] = "report.txt";
    EXPECT_EQ(write(object), R"({"name":"report.txt"})");

    Json::Value array(Json::arrayValue);
    array.append(1);
    array.append(true);
    array.append(Json::Value());
    EXPECT_EQ(write(array), "[1,true,null]");
    EXPECT_EQ(write(Json::Value()), "null");
    EXPECT_EQ(write(Json::Value(Json::objectValue)), "{}");
    EXPECT_EQ(write(Json::Value(Json::arrayValue)), "[]");
}

TEST_F(JsonCppTests, PrettyAndCompactWritersPreserveTheSameDocument)
{
    ASSERT_TRUE(parse(R"({"files":[{"name":"a.txt","size":10}],"ready":true})")) << errors_;
    const Json::Value expected = root_;
    const std::string compact = write(expected);
    const std::string pretty = write(expected, "  ");

    EXPECT_EQ(compact.find('\n'), std::string::npos);
    EXPECT_NE(pretty.find('\n'), std::string::npos);
    EXPECT_NE(pretty.find("  \"files\""), std::string::npos);
    ASSERT_TRUE(parse(compact)) << errors_;
    EXPECT_EQ(root_, expected);
    ASSERT_TRUE(parse(pretty)) << errors_;
    EXPECT_EQ(root_, expected);
}

TEST_F(JsonCppTests, WriterEscapesControlCharactersAndPreservesBinaryStrings)
{
    Json::Value object;
    object["text"] = std::string("a\0b\n\r\t\"\\", 8);
    const std::string document = write(object);

    EXPECT_NE(document.find("\\u0000"), std::string::npos);
    EXPECT_NE(document.find("\\n"), std::string::npos);
    EXPECT_NE(document.find("\\r"), std::string::npos);
    EXPECT_NE(document.find("\\t"), std::string::npos);
    EXPECT_NE(document.find("\\\""), std::string::npos);
    EXPECT_NE(document.find("\\\\"), std::string::npos);
    EXPECT_EQ(document.find('\0'), std::string::npos);
    ASSERT_TRUE(parse(document)) << errors_;
    EXPECT_EQ(root_, object);
}

TEST_F(JsonCppTests, WriterCanEmitUtf8WithoutLosingUnicode)
{
    Json::Value object;
    object[u8"文件名"] = u8"报告📦.txt";
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    const std::string document = Json::writeString(builder, object);

    EXPECT_NE(document.find(u8"文件名"), std::string::npos);
    EXPECT_NE(document.find(u8"报告📦.txt"), std::string::npos);
    ASSERT_TRUE(parse(document)) << errors_;
    EXPECT_EQ(root_, object);
}

TEST_F(JsonCppTests, WriterPreserves64BitIntegersAndDoublePrecision)
{
    Json::Value object;
    object["minimum"] = std::numeric_limits<Json::Int64>::min();
    object["maximum"] = std::numeric_limits<Json::UInt64>::max();
    object["fraction"] = 1.2345678901234567;
    const std::string document = write(object);

    EXPECT_NE(document.find("-9223372036854775808"), std::string::npos);
    EXPECT_NE(document.find("18446744073709551615"), std::string::npos);
    ASSERT_TRUE(parse(document)) << errors_;
    EXPECT_EQ(root_["minimum"].asInt64(), object["minimum"].asInt64());
    EXPECT_EQ(root_["maximum"].asUInt64(), object["maximum"].asUInt64());
    EXPECT_DOUBLE_EQ(root_["fraction"].asDouble(), object["fraction"].asDouble());
}

TEST_F(JsonCppTests, StreamWriterProducesReadableJson)
{
    Json::Value object;
    object["name"] = "stream.txt";
    object["size"] = Json::UInt64(8192);
    Json::StreamWriterBuilder builder;
    const std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    std::ostringstream stream;

    ASSERT_EQ(writer->write(object, &stream), 0);
    ASSERT_TRUE(parse(stream.str())) << errors_;
    EXPECT_EQ(root_["name"].asString(), "stream.txt");
    EXPECT_EQ(root_["size"].asUInt64(), 8192U);
}

TEST_F(JsonCppTests, RoundTripsLargeStorageMetadataCollections)
{
    Json::Value metadata(Json::objectValue);
    metadata["files"] = Json::Value(Json::arrayValue);
    for (Json::ArrayIndex i = 0; i < 1024; ++i)
    {
        Json::Value file;
        file["id"] = Json::Int(i);
        file["name"] = u8"文件-" + std::to_string(i) + ".txt";
        file["size"] = Json::Int64(1) << (i % 63);
        file["compressed"] = i % 2 == 0;
        metadata["files"].append(file);
    }

    ASSERT_TRUE(parse(write(metadata))) << errors_;
    ASSERT_EQ(root_["files"].size(), 1024U);
    EXPECT_EQ(root_, metadata);
}
