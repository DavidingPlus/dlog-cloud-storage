#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "utils/common.h"


// 十六进制与 URL 解码。

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
