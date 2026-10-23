#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

#include "utils/common.h"


namespace
{
    std::string allByteValues()
    {
        std::string bytes;
        bytes.reserve(256);
        for (unsigned int value = 0; value < 256; ++value) bytes += static_cast<char>(value);
        return bytes;
    }

    std::string repeatText(const std::string &text, size_t count)
    {
        std::string repeated;
        repeated.reserve(text.size() * count);
        for (size_t i = 0; i < count; ++i) repeated += text;
        return repeated;
    }

    // 按 0x00~0xFF 排列的固定期望值，不依赖待测编码器、解码器或十六进制工具。
    std::string percentEncodedAllBytes(bool preserveSlash)
    {
        std::string expected =
            "%00%01%02%03%04%05%06%07%08%09%0A%0B%0C%0D%0E%0F"
            "%10%11%12%13%14%15%16%17%18%19%1A%1B%1C%1D%1E%1F"
            "%20%21%22%23%24%25%26%27%28%29%2A%2B%2C-.%2F"
            "0123456789%3A%3B%3C%3D%3E%3F"
            "%40ABCDEFGHIJKLMNOPQRSTUVWXYZ%5B%5C%5D%5E_"
            "%60abcdefghijklmnopqrstuvwxyz%7B%7C%7D~%7F"
            "%80%81%82%83%84%85%86%87%88%89%8A%8B%8C%8D%8E%8F"
            "%90%91%92%93%94%95%96%97%98%99%9A%9B%9C%9D%9E%9F"
            "%A0%A1%A2%A3%A4%A5%A6%A7%A8%A9%AA%AB%AC%AD%AE%AF"
            "%B0%B1%B2%B3%B4%B5%B6%B7%B8%B9%BA%BB%BC%BD%BE%BF"
            "%C0%C1%C2%C3%C4%C5%C6%C7%C8%C9%CA%CB%CC%CD%CE%CF"
            "%D0%D1%D2%D3%D4%D5%D6%D7%D8%D9%DA%DB%DC%DD%DE%DF"
            "%E0%E1%E2%E3%E4%E5%E6%E7%E8%E9%EA%EB%EC%ED%EE%EF"
            "%F0%F1%F2%F3%F4%F5%F6%F7%F8%F9%FA%FB%FC%FD%FE%FF";

        if (preserveSlash) expected.replace(expected.find("%2F"), 3, "/");
        return expected;
    }

    // 即使测试断言失败或抛出异常，也恢复工作目录，避免影响后续用例。
    class ScopedWorkingDirectory
    {

    public:

        explicit ScopedWorkingDirectory(const std::filesystem::path &directory)
            : m_original(std::filesystem::current_path())
        {
            std::filesystem::current_path(directory);
        }

        ~ScopedWorkingDirectory()
        {
            std::error_code error;
            std::filesystem::current_path(m_original, error);
            EXPECT_FALSE(error) << error.message();
        }

        ScopedWorkingDirectory(const ScopedWorkingDirectory &) = delete;
        ScopedWorkingDirectory &operator=(const ScopedWorkingDirectory &) = delete;

    private:

        std::filesystem::path m_original;
    };
}


// 可执行文件路径与工作目录相互独立。

TEST(CommonTests, ExecutableFullPathIsAnAbsoluteRegularFile)
{
    const auto executable = utils::executableFullPath();

    EXPECT_TRUE(executable.is_absolute());
    EXPECT_FALSE(executable.filename().empty());
    EXPECT_TRUE(std::filesystem::is_regular_file(executable));
}

TEST(CommonTests, ExecutableDirectoryIsAnAbsoluteDirectory)
{
    const auto directory = utils::executableDirectory();

    EXPECT_TRUE(directory.is_absolute());
    EXPECT_TRUE(std::filesystem::is_directory(directory));
}

TEST(CommonTests, ExecutablePathsDoNotChangeWithTheWorkingDirectory)
{
    const auto executable = utils::executableFullPath();
    const auto directory = utils::executableDirectory();
    auto otherDirectory = executable.root_path();
    if (std::filesystem::equivalent(directory, otherDirectory)) otherDirectory = std::filesystem::temp_directory_path();
    if (std::filesystem::equivalent(directory, otherDirectory)) GTEST_SKIP() << "No different working directory is available";

    const ScopedWorkingDirectory workingDirectory(otherDirectory);

    EXPECT_EQ(utils::executableFullPath(), executable);
    EXPECT_EQ(utils::executableDirectory(), directory);
}


// 十六进制转换。

TEST(CommonTests, ToHexProducesUppercaseDigits)
{
    const std::string digits = "0123456789ABCDEF";

    for (unsigned char value = 0; value < 16; ++value)
    {
        EXPECT_EQ(utils::toHex(value), static_cast<unsigned char>(digits[value]));
    }
}

TEST(CommonTests, ToHexRejectsValuesOutsideOneNibble)
{
    for (unsigned int value = 16; value < 256; ++value)
    {
        SCOPED_TRACE(value);
        EXPECT_THROW(utils::toHex(static_cast<unsigned char>(value)), std::invalid_argument);
    }
}

TEST(CommonTests, FromHexAcceptsBothLetterCases)
{
    const std::string uppercase = "0123456789ABCDEF";
    const std::string lowercase = "0123456789abcdef";

    for (unsigned char value = 0; value < 16; ++value)
    {
        EXPECT_EQ(utils::fromHex(static_cast<unsigned char>(uppercase[value])), value);
        EXPECT_EQ(utils::fromHex(static_cast<unsigned char>(lowercase[value])), value);
    }
}

TEST(CommonTests, FromHexRejectsNonHexadecimalCharacters)
{
    const std::string valid = "0123456789ABCDEFabcdef";
    for (unsigned int value = 0; value < 256; ++value)
    {
        const auto character = static_cast<unsigned char>(value);
        if (valid.find(static_cast<char>(character)) != std::string::npos) continue;

        SCOPED_TRACE(value);
        EXPECT_THROW(utils::fromHex(character), std::invalid_argument);
    }
}


// 百分号编码：两种斜杠策略共享相同的字节编码规则。

TEST(CommonTests, PercentEncodeHandlesEmptyInput)
{
    EXPECT_EQ(utils::percentEncode("", false), "");
    EXPECT_EQ(utils::percentEncode("", true), "");
}

TEST(CommonTests, PercentEncodePreservesAllUnreservedCharacters)
{
    const std::string unreserved = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";

    EXPECT_EQ(utils::percentEncode(unreserved, false), unreserved);
    EXPECT_EQ(utils::percentEncode(unreserved, true), unreserved);
}

TEST(CommonTests, PercentEncodeEscapesReservedCharacters)
{
    const std::string reserved = ":/?#[]@!$&'()*+,;=";

    EXPECT_EQ(utils::percentEncode(reserved, false),
              "%3A%2F%3F%23%5B%5D%40%21%24%26%27%28%29%2A%2B%2C%3B%3D");
    EXPECT_EQ(utils::percentEncode(reserved, true),
              "%3A/%3F%23%5B%5D%40%21%24%26%27%28%29%2A%2B%2C%3B%3D");
}

TEST(CommonTests, PercentEncodeOnlyPreservesSlashWhenRequested)
{
    const std::string text = "/a b/+%?#\\";

    EXPECT_EQ(utils::percentEncode(text, false), "%2Fa%20b%2F%2B%25%3F%23%5C");
    EXPECT_EQ(utils::percentEncode(text, true), "/a%20b/%2B%25%3F%23%5C");
}

TEST(CommonTests, PercentEncodeEncodesUtf8BytesWithUppercaseHexDigits)
{
    const std::string text = u8"云存储/😀 a+b.txt";

    EXPECT_EQ(utils::percentEncode(text, false),
              "%E4%BA%91%E5%AD%98%E5%82%A8%2F%F0%9F%98%80%20a%2Bb.txt");
    EXPECT_EQ(utils::percentEncode(text, true),
              "%E4%BA%91%E5%AD%98%E5%82%A8/%F0%9F%98%80%20a%2Bb.txt");
}

TEST(CommonTests, PercentEncodeTreatsExistingEscapesAsRawText)
{
    // 合法、非法及未完成的百分号序列都只是原始文本，无需预先解码或校验。
    for (const bool preserveSlash : {false, true})
    {
        SCOPED_TRACE(preserveSlash);
        EXPECT_EQ(utils::percentEncode("%2f%41%GG%", preserveSlash), "%252f%2541%25GG%25");
        EXPECT_EQ(utils::percentEncode("a+b c", preserveSlash), "a%2Bb%20c");
    }
}

TEST(CommonTests, PercentEncodeHandlesEmbeddedNulsAndHighBytes)
{
    const std::string bytes("\0\x01\x7f\x80\xff", 5);

    for (const bool preserveSlash : {false, true})
    {
        SCOPED_TRACE(preserveSlash);
        EXPECT_EQ(utils::percentEncode(bytes, preserveSlash), "%00%01%7F%80%FF");
        EXPECT_EQ(utils::percentEncode(std::string("a\0b", 3), preserveSlash), "a%00b");
    }
}

TEST(CommonTests, PercentEncodeMatchesExpectedEncodingForEveryByte)
{
    const auto bytes = allByteValues();

    for (const bool preserveSlash : {false, true})
    {
        SCOPED_TRACE(preserveSlash);
        EXPECT_EQ(utils::percentEncode(bytes, preserveSlash), percentEncodedAllBytes(preserveSlash));
    }
}

TEST(CommonTests, PercentEncodeHandlesLongMixedInput)
{
    const auto text = repeatText(u8"a /云+%", 4096);

    EXPECT_EQ(utils::percentEncode(text, false), repeatText("a%20%2F%E4%BA%91%2B%25", 4096));
    EXPECT_EQ(utils::percentEncode(text, true), repeatText("a%20/%E4%BA%91%2B%25", 4096));
}


// URL 解码与编码后的往返。

TEST(CommonTests, UrlDecodePreservesUnescapedText)
{
    const std::string inputs[] = {"", "file.txt", "/download/a+b.txt", u8"云存储.txt"};

    for (const auto &input : inputs)
    {
        SCOPED_TRACE(input);
        EXPECT_EQ(utils::urlDecode(input), input);
    }
}

TEST(CommonTests, UrlDecodeHandlesUtf8AndMixedCaseEscapes)
{
    EXPECT_EQ(utils::urlDecode("%e4%BA%91%E5%ad%98%e5%82%A8%20a%2Bb.txt"),
              u8"云存储 a+b.txt");
    EXPECT_EQ(utils::urlDecode("%2f%2F%3a"), "//:");
}

TEST(CommonTests, UrlDecodePreservesPlusAndDecodesOnlyOnce)
{
    EXPECT_EQ(utils::urlDecode("a+b%2Bc%20d"), "a+b+c d");
    EXPECT_EQ(utils::urlDecode("%25"), "%");
    EXPECT_EQ(utils::urlDecode("%252F"), "%2F");
    EXPECT_EQ(utils::urlDecode("%25%32%46"), "%2F");
}

TEST(CommonTests, UrlDecodePreservesNulsAndHighBytes)
{
    const std::string bytes("\0\x01\x7f\x80\xff", 5);

    EXPECT_EQ(utils::urlDecode("%00%01%7F%80%FF"), bytes);
    EXPECT_EQ(utils::urlDecode(bytes), bytes);
    EXPECT_EQ(utils::urlDecode(std::string("a\0%20b", 6)), std::string("a\0 b", 4));
}

TEST(CommonTests, UrlDecodeHandlesEveryEscapedByte)
{
    const std::string digits = "0123456789ABCDEF";
    std::string encoded;
    std::string expected;

    // 独立构造所有字节的编码，避免使用待测的 toHex() 生成输入。
    for (unsigned int value = 0; value < 256; ++value)
    {
        encoded.push_back('%');
        encoded.push_back(digits[value / 16]);
        encoded.push_back(digits[value % 16]);
        expected.push_back(static_cast<char>(value));
    }

    EXPECT_EQ(utils::urlDecode(encoded), expected);
}

TEST(CommonTests, UrlDecodeRejectsMalformedEscapes)
{
    const std::string inputs[] = {
        "%", "%0", "file%", "file%A", "%GG", "%G0", "%0g", "%1Z",
        "%Z1", "% 0", "%%20", "valid%20then%", std::string("%\0A", 3)};

    for (const auto &input : inputs)
    {
        SCOPED_TRACE(input);
        EXPECT_THROW(utils::urlDecode(input), std::invalid_argument);
    }
}

TEST(CommonTests, UrlDecodeRejectsEveryInvalidHexByteInEitherPosition)
{
    const std::string valid = "0123456789ABCDEFabcdef";
    for (unsigned int value = 0; value < 256; ++value)
    {
        const auto character = static_cast<char>(value);
        if (valid.find(character) != std::string::npos) continue;

        SCOPED_TRACE(value);
        EXPECT_THROW(utils::urlDecode(std::string("%") + character + '0'), std::invalid_argument);
        EXPECT_THROW(utils::urlDecode(std::string("%0") + character), std::invalid_argument);
    }
}
