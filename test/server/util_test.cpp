#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "server/util.h"


TEST(ServerUtilTests, ToHexProducesUppercaseDigits)
{
    const std::string digits = "0123456789ABCDEF";

    for (unsigned char value = 0; value < 16; ++value)
    {
        EXPECT_EQ(server::toHex(value), static_cast<unsigned char>(digits[value]));
    }
}

TEST(ServerUtilTests, ToHexRejectsValuesOutsideOneNibble)
{
    EXPECT_THROW(server::toHex(16), std::invalid_argument);
    EXPECT_THROW(server::toHex(255), std::invalid_argument);
}

TEST(ServerUtilTests, FromHexAcceptsBothLetterCases)
{
    const std::string uppercase = "0123456789ABCDEF";
    const std::string lowercase = "0123456789abcdef";

    for (unsigned char value = 0; value < 16; ++value)
    {
        EXPECT_EQ(server::fromHex(static_cast<unsigned char>(uppercase[value])), value);
        EXPECT_EQ(server::fromHex(static_cast<unsigned char>(lowercase[value])), value);
    }
}

TEST(ServerUtilTests, FromHexRejectsNonHexadecimalCharacters)
{
    const unsigned char invalid[] = {'/', ':', '@', 'G', 'Z', '[', '`', 'g', 'z', '{', ' ', 0, 255};

    for (const unsigned char character : invalid)
    {
        SCOPED_TRACE(static_cast<unsigned int>(character));
        EXPECT_THROW(server::fromHex(character), std::invalid_argument);
    }
}

TEST(ServerUtilTests, UrlDecodePreservesUnescapedText)
{
    const std::string inputs[] = {"", "file.txt", "/download/a+b.txt", u8"云存储.txt"};

    for (const auto &input : inputs)
    {
        SCOPED_TRACE(input);
        EXPECT_EQ(server::urlDecode(input), input);
    }
}

TEST(ServerUtilTests, UrlDecodeHandlesUtf8AndMixedCaseEscapes)
{
    EXPECT_EQ(server::urlDecode("%e4%BA%91%E5%ad%98%e5%82%A8%20a%2Bb.txt"),
              u8"云存储 a+b.txt");
    EXPECT_EQ(server::urlDecode("%2f%2F%3a"), "//:");
}

TEST(ServerUtilTests, UrlDecodePreservesPlusAndDecodesOnlyOnce)
{
    EXPECT_EQ(server::urlDecode("a+b%2Bc%20d"), "a+b+c d");
    EXPECT_EQ(server::urlDecode("%25"), "%");
    EXPECT_EQ(server::urlDecode("%252F"), "%2F");
    EXPECT_EQ(server::urlDecode("%25%32%46"), "%2F");
}

TEST(ServerUtilTests, UrlDecodePreservesNulsAndHighBytes)
{
    const std::string bytes("\0\x01\x7f\x80\xff", 5);

    EXPECT_EQ(server::urlDecode("%00%01%7F%80%FF"), bytes);
    EXPECT_EQ(server::urlDecode(bytes), bytes);
    EXPECT_EQ(server::urlDecode(std::string("a\0%20b", 6)), std::string("a\0 b", 4));
}

TEST(ServerUtilTests, UrlDecodeHandlesEveryEscapedByte)
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

    EXPECT_EQ(server::urlDecode(encoded), expected);
}

TEST(ServerUtilTests, UrlDecodeRejectsMalformedEscapes)
{
    const std::string inputs[] = {
        "%", "%0", "file%", "file%A", "%GG", "%G0", "%0g", "%1Z",
        "%Z1", "% 0", "%%20", "valid%20then%", std::string("%\0A", 3)};

    for (const auto &input : inputs)
    {
        SCOPED_TRACE(input);
        EXPECT_THROW(server::urlDecode(input), std::invalid_argument);
    }
}
