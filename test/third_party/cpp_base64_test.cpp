#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

#include <base64.h>


TEST(CppBase64Tests, Encode)
{
    const std::string s =
        "René Nyffenegger\n"
        "http://www.renenyffenegger.ch\n"
        "passion for data\n";

    std::string encoded = base64_encode(reinterpret_cast<const unsigned char *>(s.c_str()), s.length());
    std::string decoded = base64_decode(encoded);

    ASSERT_EQ(decoded, s);

    std::cout << "encoded: " << std::endl
              << encoded << std::endl
              << std::endl;
    std::cout << "decoded: " << std::endl
              << decoded << std::endl;

    // Test all possibilites of fill bytes (none, one =, two ==)
    // References calculated with: https://www.base64encode.org/

    std::string rest0_original = "abc";
    std::string rest0_reference = "YWJj";

    std::string rest0_encoded = base64_encode(reinterpret_cast<const unsigned char *>(rest0_original.c_str()),
                                              rest0_original.length());
    std::string rest0_decoded = base64_decode(rest0_encoded);

    ASSERT_EQ(rest0_decoded, rest0_original);

    std::cout << "encoded:   " << rest0_encoded << std::endl;
    std::cout << "reference: " << rest0_reference << std::endl;
    std::cout << "decoded:   " << rest0_decoded << std::endl
              << std::endl;

    std::string rest1_original = "abcd";
    std::string rest1_reference = "YWJjZA==";

    std::string rest1_encoded = base64_encode(reinterpret_cast<const unsigned char *>(rest1_original.c_str()),
                                              rest1_original.length());
    std::string rest1_decoded = base64_decode(rest1_encoded);

    ASSERT_EQ(rest1_decoded, rest1_original);

    std::cout << "encoded:   " << rest1_encoded << std::endl;
    std::cout << "reference: " << rest1_reference << std::endl;
    std::cout << "decoded:   " << rest1_decoded << std::endl
              << std::endl;

    std::string rest2_original = "abcde";
    std::string rest2_reference = "YWJjZGU=";

    std::string rest2_encoded = base64_encode(reinterpret_cast<const unsigned char *>(rest2_original.c_str()),
                                              rest2_original.length());
    std::string rest2_decoded = base64_decode(rest2_encoded);

    ASSERT_EQ(rest2_decoded, rest2_original);

    std::cout << "encoded:   " << rest2_encoded << std::endl;
    std::cout << "reference: " << rest2_reference << std::endl;
    std::cout << "decoded:   " << rest2_decoded << std::endl
              << std::endl;
}

TEST(CppBase64Tests, MatchesRfc4648VectorsIncludingEmptyInputAndPadding)
{
    const struct
    {
        std::string plain;
        std::string encoded;
    } vectors[] = {
        {"", ""},
        {"f", "Zg=="},
        {"fo", "Zm8="},
        {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="},
        {"fooba", "Zm9vYmE="},
        {"foobar", "Zm9vYmFy"}};

    for (const auto &vector : vectors)
    {
        SCOPED_TRACE(vector.plain);
        EXPECT_EQ(base64_encode(vector.plain), vector.encoded);
        EXPECT_EQ(base64_decode(vector.encoded), vector.plain);
        EXPECT_EQ(base64_encode(std::string_view(vector.plain)), vector.encoded);
        EXPECT_EQ(base64_decode(std::string_view(vector.encoded)), vector.plain);
    }
}

TEST(CppBase64Tests, PreservesEmbeddedNulsAndHighBytes)
{
    const std::string input("\x00\x01\x7f\x80\xfe\xff\x00\xa5", 8);
    const std::string expected = "AAF/gP7/AKU=";

    EXPECT_EQ(base64_encode(input), expected);
    EXPECT_EQ(base64_decode(expected), input);
    EXPECT_EQ(base64_encode(std::string_view(input)), expected);
    EXPECT_EQ(base64_decode(std::string_view(expected)), input);
}

TEST(CppBase64Tests, PointerApiRespectsExplicitLength)
{
    const unsigned char input[] = {0xff, 'f', 'o', 'o', 0x00, 0xfe};

    EXPECT_EQ(base64_encode(input + 1, 0), "");
    EXPECT_EQ(base64_encode(input + 1, 3), "Zm9v");
    EXPECT_EQ(base64_encode(input + 1, 4), "Zm9vAA==");
}

TEST(CppBase64Tests, StringViewApiRespectsSubstringBoundaries)
{
    const std::string plain = "!foobar?";
    const std::string encoded = "!Zm9vYmFy?";
    const std::string_view plain_view(plain.data() + 1, 6);
    const std::string_view encoded_view(encoded.data() + 1, 8);

    EXPECT_EQ(base64_encode(plain_view), "Zm9vYmFy");
    EXPECT_EQ(base64_decode(encoded_view), "foobar");
    EXPECT_EQ(base64_encode_pem(plain_view), "Zm9vYmFy");
    EXPECT_EQ(base64_encode_mime(plain_view), "Zm9vYmFy");
}

TEST(CppBase64Tests, UrlModeUsesAlternateAlphabetAndDotPadding)
{
    const struct
    {
        std::string plain;
        std::string encoded;
    } vectors[] = {
        {"", ""},
        {std::string("\xfb", 1), "-w.."},
        {std::string("\xfb\xff", 2), "-_8."},
        {std::string("\xfb\xff\xff", 3), "-___"}};

    // 此版本 URL 模式使用 '.' 补齐；它与省略补齐的 Base64URL 格式不同。
    for (const auto &vector : vectors)
    {
        SCOPED_TRACE(vector.encoded);
        EXPECT_EQ(base64_encode(vector.plain, true), vector.encoded);
        EXPECT_EQ(base64_encode(std::string_view(vector.plain), true), vector.encoded);
        EXPECT_EQ(base64_encode(reinterpret_cast<const unsigned char *>(vector.plain.data()),
                                vector.plain.size(), true),
                  vector.encoded);
        EXPECT_EQ(base64_decode(vector.encoded), vector.plain);
        EXPECT_EQ(base64_decode(std::string_view(vector.encoded)), vector.plain);
    }
}

TEST(CppBase64Tests, AcceptsUnpaddedStandardAndUrlEncodedInput)
{
    const struct
    {
        std::string encoded;
        std::string plain;
    } vectors[] = {
        {"Zg", "f"},
        {"Zm8", "fo"},
        {"Zm9vYg", "foob"},
        {"+/8", std::string("\xfb\xff", 2)},
        {"-_8", std::string("\xfb\xff", 2)}};

    for (const auto &vector : vectors)
    {
        SCOPED_TRACE(vector.encoded);
        EXPECT_EQ(base64_decode(vector.encoded), vector.plain);
        EXPECT_EQ(base64_decode(std::string_view(vector.encoded)), vector.plain);
    }
}

TEST(CppBase64Tests, PemWrapsAt64ColumnsWithoutTrailingNewline)
{
    const std::string empty;
    const std::string at_boundary(48, 'a');
    const std::string over_boundary(49, 'a');
    const std::string multiple_lines(97, 'a');
    const std::string line = "YWFhYWFhYWFhYWFh"
                             "YWFhYWFhYWFhYWFh"
                             "YWFhYWFhYWFhYWFh"
                             "YWFhYWFhYWFhYWFh";

    EXPECT_TRUE(base64_encode_pem(empty).empty());
    EXPECT_EQ(base64_encode_pem(at_boundary), line);
    EXPECT_EQ(base64_encode_pem(over_boundary), line + "\nYQ==");
    EXPECT_EQ(base64_encode_pem(std::string_view(multiple_lines)), line + "\n" + line + "\nYQ==");
    EXPECT_EQ(base64_decode(base64_encode_pem(multiple_lines), true), multiple_lines);
}

TEST(CppBase64Tests, MimeWrapsAt76ColumnsWithoutTrailingNewline)
{
    const std::string empty;
    const std::string at_boundary(57, 'a');
    const std::string over_boundary(58, 'a');
    const std::string multiple_lines(115, 'a');
    const std::string line = "YWFhYWFhYWFhYWFh"
                             "YWFhYWFhYWFhYWFh"
                             "YWFhYWFhYWFhYWFh"
                             "YWFhYWFhYWFhYWFh"
                             "YWFhYWFhYWFh";

    EXPECT_TRUE(base64_encode_mime(empty).empty());
    EXPECT_EQ(base64_encode_mime(at_boundary), line);
    EXPECT_EQ(base64_encode_mime(over_boundary), line + "\nYQ==");
    EXPECT_EQ(base64_encode_mime(std::string_view(multiple_lines)), line + "\n" + line + "\nYQ==");
    EXPECT_EQ(base64_decode(base64_encode_mime(multiple_lines), true), multiple_lines);
}

TEST(CppBase64Tests, RemovesLinefeedsWhenRequested)
{
    const std::string encoded = "\nZm9v\nYmFy\n";

    EXPECT_EQ(base64_decode(encoded, true), "foobar");
    EXPECT_EQ(base64_decode(std::string_view(encoded), true), "foobar");
    EXPECT_THROW(base64_decode(encoded), std::runtime_error);
}

TEST(CppBase64Tests, RejectsCharactersOutsideBothAlphabets)
{
    const std::string inputs[] = {"!m9v", "Z!9v", "Zm!v", "Zm9!", "Zm v", "Zm\tv", "Zm\rv"};

    for (const auto &input : inputs)
    {
        SCOPED_TRACE(input);
        EXPECT_THROW(base64_decode(input), std::runtime_error);
        EXPECT_THROW(base64_decode(std::string_view(input)), std::runtime_error);
        EXPECT_THROW(base64_decode(input, true), std::runtime_error);
    }
}

TEST(CppBase64Tests, RoundTripsAllByteValuesAcrossBlockBoundaries)
{
    const std::size_t sizes[] = {0, 1, 2, 3, 4, 5, 47, 48, 49, 56, 57, 58, 255, 256, 257, 65537};

    for (std::size_t size : sizes)
    {
        SCOPED_TRACE(size);
        std::string input;
        input.reserve(size);
        for (std::size_t i = 0; i < size; ++i)
        {
            input.push_back(static_cast<char>(i % 256));
        }

        for (bool url : {false, true})
        {
            SCOPED_TRACE(url);
            const std::string encoded = base64_encode(input, url);
            EXPECT_EQ(encoded.size(), (size + 2) / 3 * 4);
            EXPECT_EQ(base64_decode(encoded), input);
            EXPECT_EQ(base64_encode(std::string_view(input), url), encoded);
            EXPECT_EQ(base64_decode(std::string_view(encoded)), input);
        }
    }
}
