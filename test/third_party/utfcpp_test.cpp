#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <utf8.h>


namespace
{
    std::string bytes(std::initializer_list<unsigned int> values)
    {
        std::string result;
        for (auto value : values) result.push_back(static_cast<char>(value));
        return result;
    }

    struct EncodingCase
    {
        const char *m_name;
        std::string m_utf8;
        std::u16string m_utf16;
        std::u32string m_utf32;
    };

    const EncodingCase &mixedEncoding()
    {
        // 固定期望值，不依赖库生成测试输入；使用 char16_t/char32_t，避免 wchar_t 的平台差异。
        static const EncodingCase value{
            "ASCII, two/three/four-byte characters",
            bytes({0x41, 0xc2, 0xa2, 0xe2, 0x82, 0xac, 0xe4, 0xb8, 0xad, 0xf0, 0x9f, 0x98, 0x80, 0x5a}),
            u"A\u00a2\u20ac\u4e2d\U0001f600Z",
            U"A\u00a2\u20ac\u4e2d\U0001f600Z"};
        return value;
    }

    const std::vector<EncodingCase> &validEncodings()
    {
        static const std::vector<EncodingCase> values{
            {"empty", {}, {}, {}},
            {"embedded NUL", bytes({0x41, 0, 0x5a}), std::u16string{0x41, 0, 0x5a}, std::u32string{0x41, 0, 0x5a}},
            mixedEncoding(),
            {"Unicode scalar boundaries",
             bytes({0, 0x7f, 0xc2, 0x80, 0xdf, 0xbf, 0xe0, 0xa0, 0x80, 0xed, 0x9f, 0xbf,
                    0xee, 0x80, 0x80, 0xef, 0xbf, 0xbf, 0xf0, 0x90, 0x80, 0x80, 0xf4, 0x8f, 0xbf, 0xbf}),
             std::u16string{0, 0x7f, 0x80, 0x7ff, 0x800, 0xd7ff, 0xe000, 0xffff, 0xd800, 0xdc00, 0xdbff, 0xdfff},
             std::u32string{0, 0x7f, 0x80, 0x7ff, 0x800, 0xd7ff, 0xe000, 0xffff, 0x10000, 0x10ffff}},
            {"BOM is preserved", bytes({0xef, 0xbb, 0xbf, 0x41}), u"\ufeffA", U"\ufeffA"},
            {"combining sequence is preserved", bytes({0x65, 0xcc, 0x81}), u"e\u0301", U"e\u0301"}};
        return values;
    }

    struct InvalidCase
    {
        const char *m_name;
        std::string m_bytes;
    };

    const std::vector<InvalidCase> &invalidEncodings()
    {
        static const std::vector<InvalidCase> values{
            {"lone continuation", bytes({0x80})},
            {"highest continuation", bytes({0xbf})},
            {"invalid lead FE", bytes({0xfe})},
            {"invalid lead FF", bytes({0xff})},
            {"truncated two-byte sequence", bytes({0xc2})},
            {"truncated three-byte lead", bytes({0xe2})},
            {"truncated three-byte sequence", bytes({0xe2, 0x82})},
            {"truncated four-byte sequence", bytes({0xf0, 0x9f, 0x98})},
            {"ASCII instead of continuation", bytes({0xc2, 0x41})},
            {"invalid second byte", bytes({0xe2, 0x28, 0xa1})},
            {"invalid third byte", bytes({0xe2, 0x82, 0x41})},
            {"invalid fourth byte", bytes({0xf0, 0x9f, 0x98, 0x41})},
            {"two-byte overlong slash", bytes({0xc0, 0xaf})},
            {"two-byte overlong NUL", bytes({0xc0, 0x80})},
            {"overlong C1 lead", bytes({0xc1, 0xbf})},
            {"three-byte overlong slash", bytes({0xe0, 0x80, 0xaf})},
            {"four-byte overlong slash", bytes({0xf0, 0x80, 0x80, 0xaf})},
            {"high surrogate", bytes({0xed, 0xa0, 0x80})},
            {"low surrogate", bytes({0xed, 0xbf, 0xbf})},
            {"CESU-8 surrogate pair", bytes({0xed, 0xa0, 0xbd, 0xed, 0xb8, 0x80})},
            {"above U+10FFFF", bytes({0xf4, 0x90, 0x80, 0x80})},
            {"invalid F5 lead", bytes({0xf5, 0x80, 0x80, 0x80})},
            {"obsolete five-byte sequence", bytes({0xf8, 0x88, 0x80, 0x80, 0x80})},
            {"obsolete six-byte sequence", bytes({0xfc, 0x84, 0x80, 0x80, 0x80, 0x80})}};
        return values;
    }
}


TEST(UtfCppValidationTests, AcceptsValidUnicodeScalarsAndEmptyInput)
{
    for (const auto &value : validEncodings())
    {
        SCOPED_TRACE(value.m_name);
        EXPECT_TRUE(utf8::is_valid(value.m_utf8.begin(), value.m_utf8.end()));
        EXPECT_TRUE(utf8::is_valid(value.m_utf8));
        EXPECT_TRUE(utf8::is_valid(std::string_view(value.m_utf8)));
        EXPECT_EQ(utf8::find_invalid(value.m_utf8.begin(), value.m_utf8.end()), value.m_utf8.end());
    }
}

TEST(UtfCppValidationTests, EncodingValidationAcceptsAllAsciiIncludingControls)
{
    std::string input;
    for (unsigned int value = 0; value < 128; ++value) input.push_back(static_cast<char>(value));
    // 编码校验不承担文件名、路径或其他业务规则校验。
    EXPECT_TRUE(utf8::is_valid(input));
    EXPECT_TRUE(utf8::is_valid(std::string_view(input)));
    EXPECT_EQ(utf8::distance(input.begin(), input.end()), 128);
}

TEST(UtfCppValidationTests, RejectsMalformedSequencesWithoutThrowing)
{
    for (const auto &value : invalidEncodings())
    {
        SCOPED_TRACE(value.m_name);
        bool valid = true;
        EXPECT_NO_THROW(valid = utf8::is_valid(value.m_bytes.begin(), value.m_bytes.end()));
        EXPECT_FALSE(valid);
        EXPECT_FALSE(utf8::is_valid(value.m_bytes));
        EXPECT_FALSE(utf8::is_valid(std::string_view(value.m_bytes)));
    }
}

TEST(UtfCppValidationTests, BoundedApisValidateBytesAfterEmbeddedNul)
{
    const auto input = bytes({0x41, 0, 0xff});
    EXPECT_TRUE(utf8::is_valid(input.c_str())); // C 字符串接口只看到第一个 NUL。
    EXPECT_FALSE(utf8::is_valid(input));
    EXPECT_FALSE(utf8::is_valid(std::string_view(input)));
    EXPECT_FALSE(utf8::is_valid(input.begin(), input.end()));
    EXPECT_EQ(utf8::find_invalid(input), 2U);
    EXPECT_EQ(utf8::find_invalid(std::string_view(input)), 2U);
}

TEST(UtfCppValidationTests, HonorsExplicitRangeBounds)
{
    const auto input = bytes({0xff}) + mixedEncoding().m_utf8 + bytes({0xff});
    const std::string_view view(input.data() + 1, mixedEncoding().m_utf8.size());
    EXPECT_FALSE(utf8::is_valid(input));
    EXPECT_TRUE(utf8::is_valid(view.begin(), view.end()));
    EXPECT_TRUE(utf8::is_valid(view));
    EXPECT_EQ(utf8::find_invalid(view), std::string_view::npos);
    EXPECT_FALSE(utf8::is_valid(view.begin(), view.begin() + 2)); // 截断两字节字符。
}

TEST(UtfCppValidationTests, FindsTheFirstInvalidByteAfterAMultibytePrefix)
{
    const auto prefix = bytes({0x41, 0xe4, 0xb8, 0xad});
    for (const auto &value : invalidEncodings())
    {
        SCOPED_TRACE(value.m_name);
        const auto input = prefix + value.m_bytes;
        EXPECT_EQ(utf8::find_invalid(input.begin(), input.end()), input.begin() + prefix.size());
        EXPECT_EQ(utf8::find_invalid(input), prefix.size());
    }
}

TEST(UtfCppValidationTests, SupportsUnsignedByteIterators)
{
    const auto &expected = mixedEncoding();
    std::vector<unsigned char> input(expected.m_utf8.begin(), expected.m_utf8.end());
    EXPECT_TRUE(utf8::is_valid(input.begin(), input.end()));
    auto it = input.begin();
    for (auto codePoint : expected.m_utf32) EXPECT_EQ(utf8::next(it, input.end()), codePoint);
    EXPECT_EQ(it, input.end());
    input.push_back(0xff);
    EXPECT_FALSE(utf8::is_valid(input.begin(), input.end()));
}

TEST(UtfCppTraversalTests, ReadsCodePointsAtKnownByteOffsets)
{
    const auto &value = mixedEncoding();
    const std::vector<std::ptrdiff_t> offsets{0, 1, 3, 6, 9, 13, 14};
    auto it = value.m_utf8.begin();
    for (size_t index = 0; index < value.m_utf32.size(); ++index)
    {
        EXPECT_EQ(std::distance(value.m_utf8.begin(), it), offsets[index]);
        EXPECT_EQ(utf8::next(it, value.m_utf8.end()), value.m_utf32[index]);
    }
    EXPECT_EQ(std::distance(value.m_utf8.begin(), it), offsets.back());
}

TEST(UtfCppTraversalTests, PeekDoesNotAdvanceTheIterator)
{
    const auto &value = mixedEncoding();
    auto it = value.m_utf8.begin() + 9;
    const auto original = it;
    EXPECT_EQ(utf8::peek_next(it, value.m_utf8.end()), 0x1f600U);
    EXPECT_EQ(it, original);
}

TEST(UtfCppTraversalTests, TraversesBackwardOverMultibyteCharacters)
{
    const auto &value = mixedEncoding();
    auto it = value.m_utf8.end();
    for (auto expected = value.m_utf32.rbegin(); expected != value.m_utf32.rend(); ++expected)
        EXPECT_EQ(utf8::prior(it, value.m_utf8.begin()), *expected);
    EXPECT_EQ(it, value.m_utf8.begin());
}

TEST(UtfCppTraversalTests, CountsAndAdvancesCodePointsInsteadOfBytes)
{
    const auto &value = mixedEncoding();
    EXPECT_EQ(utf8::distance(value.m_utf8.begin(), value.m_utf8.end()), 6);
    auto it = value.m_utf8.begin();
    utf8::advance(it, 4, value.m_utf8.end());
    EXPECT_EQ(std::distance(value.m_utf8.begin(), it), 9);
    EXPECT_EQ(utf8::next(it, value.m_utf8.end()), 0x1f600U);
}

TEST(UtfCppTraversalTests, CheckedNextReportsInvalidTruncatedAndEmptyInput)
{
    auto invalid = bytes({0x80});
    auto invalidIt = invalid.begin();
    EXPECT_THROW(utf8::next(invalidIt, invalid.end()), utf8::invalid_utf8);
    auto truncated = bytes({0xe2, 0x82});
    auto truncatedIt = truncated.begin();
    EXPECT_THROW(utf8::next(truncatedIt, truncated.end()), utf8::not_enough_room);
    auto surrogate = bytes({0xed, 0xa0, 0x80});
    auto surrogateIt = surrogate.begin();
    EXPECT_THROW(utf8::next(surrogateIt, surrogate.end()), utf8::invalid_code_point);
    const std::string empty;
    auto emptyIt = empty.begin();
    EXPECT_THROW(utf8::next(emptyIt, empty.end()), utf8::not_enough_room);
}

TEST(UtfCppTraversalTests, CheckedIteratorSupportsForwardAndBackwardTraversal)
{
    const auto &value = mixedEncoding();
    using Iterator = utf8::iterator<std::string::const_iterator>;
    const Iterator begin(value.m_utf8.begin(), value.m_utf8.begin(), value.m_utf8.end());
    const Iterator end(value.m_utf8.end(), value.m_utf8.begin(), value.m_utf8.end());
    auto it = begin;
    for (auto codePoint : value.m_utf32)
    {
        ASSERT_NE(it, end);
        EXPECT_EQ(*it++, codePoint);
    }
    EXPECT_EQ(it, end);
    for (auto expected = value.m_utf32.rbegin(); expected != value.m_utf32.rend(); ++expected) EXPECT_EQ(*--it, *expected);
    EXPECT_EQ(it, begin);
}

TEST(UtfCppConversionTests, MatchesFixedUtf8Utf16AndUtf32Vectors)
{
    for (const auto &value : validEncodings())
    {
        SCOPED_TRACE(value.m_name);
        EXPECT_EQ(utf8::utf8to16(value.m_utf8), value.m_utf16);
        EXPECT_EQ(utf8::utf16to8(value.m_utf16), value.m_utf8);
        EXPECT_EQ(utf8::utf8to32(value.m_utf8), value.m_utf32);
        EXPECT_EQ(utf8::utf32to8(value.m_utf32), value.m_utf8);
    }
}

TEST(UtfCppConversionTests, IteratorConversionsRespectRangesAndAppendToOutput)
{
    const auto &value = mixedEncoding();
    const auto input = bytes({0xff}) + value.m_utf8 + bytes({0xff});
    std::u16string output16 = u"prefix:";
    utf8::utf8to16(input.begin() + 1, input.end() - 1, std::back_inserter(output16));
    EXPECT_EQ(output16, u"prefix:" + value.m_utf16);
    std::u32string output32 = U"prefix:";
    utf8::utf8to32(input.begin() + 1, input.end() - 1, std::back_inserter(output32));
    EXPECT_EQ(output32, U"prefix:" + value.m_utf32);
    std::string output8 = "prefix:";
    utf8::utf16to8(value.m_utf16.begin(), value.m_utf16.end(), std::back_inserter(output8));
    EXPECT_EQ(output8, "prefix:" + value.m_utf8);
    output8 = "prefix:";
    utf8::utf32to8(value.m_utf32.begin(), value.m_utf32.end(), std::back_inserter(output8));
    EXPECT_EQ(output8, "prefix:" + value.m_utf8);
}

TEST(UtfCppConversionTests, StringViewOverloadsPreserveNulsAndExcludeSurroundingData)
{
    for (const auto &value : validEncodings())
    {
        SCOPED_TRACE(value.m_name);
        const auto input8 = bytes({0xff}) + value.m_utf8 + bytes({0xff});
        const auto input16 = std::u16string{0xdc00} + value.m_utf16 + std::u16string{0xd800};
        const auto input32 = std::u32string{0x110000} + value.m_utf32 + std::u32string{0x110000};
        EXPECT_EQ(utf8::utf8to16(std::string_view(input8.data() + 1, value.m_utf8.size())), value.m_utf16);
        EXPECT_EQ(utf8::utf8to32(std::string_view(input8.data() + 1, value.m_utf8.size())), value.m_utf32);
        EXPECT_EQ(utf8::utf16to8(std::u16string_view(input16.data() + 1, value.m_utf16.size())), value.m_utf8);
        EXPECT_EQ(utf8::utf32to8(std::u32string_view(input32.data() + 1, value.m_utf32.size())), value.m_utf8);
    }
}

TEST(UtfCppConversionTests, RejectsMalformedUtf8DuringConversion)
{
    for (const auto &value : invalidEncodings())
    {
        SCOPED_TRACE(value.m_name);
        EXPECT_THROW(utf8::utf8to16(value.m_bytes), utf8::exception);
        EXPECT_THROW(utf8::utf8to32(value.m_bytes), utf8::exception);
    }
}

TEST(UtfCppConversionTests, RejectsUnpairedUtf16Surrogates)
{
    const std::vector<std::u16string> inputs{{0xd800}, {0xdfff}, {0xd800, 0x41}, {0xd800, 0xdbff}, {0xdc00, 0xd800}};
    for (size_t index = 0; index < inputs.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_THROW(utf8::utf16to8(inputs[index]), utf8::invalid_utf16);
    }
}

TEST(UtfCppConversionTests, RejectsInvalidUtf32ScalarsAndPreservesTheErrorCodePoint)
{
    for (std::uint32_t codePoint : {0xd800U, 0xdbffU, 0xdc00U, 0xdfffU, 0x110000U, 0xffffffffU})
    {
        SCOPED_TRACE(codePoint);
        const std::u32string input{static_cast<char32_t>(codePoint)};
        EXPECT_THROW(utf8::utf32to8(input), utf8::invalid_code_point);
        std::string output;
        try
        {
            utf8::append(codePoint, std::back_inserter(output));
            FAIL() << "Expected invalid_code_point";
        }
        catch (const utf8::invalid_code_point &error)
        {
            EXPECT_EQ(error.code_point(), codePoint);
        }
    }
}

TEST(UtfCppConversionTests, AppendsKnownEncodingsAtAllScalarBoundaries)
{
    for (const auto &value : validEncodings())
    {
        SCOPED_TRACE(value.m_name);
        std::string output;
        for (auto codePoint : value.m_utf32) utf8::append(codePoint, output);
        EXPECT_EQ(output, value.m_utf8);
    }
}

TEST(UtfCppReplacementTests, ReplacesInvalidAndTruncatedSequencesWithKnownOutput)
{
    const auto input = bytes({0x41, 0x80, 0x5a, 0xe2, 0x82});
    const auto replacement = bytes({0xef, 0xbf, 0xbd});
    const auto expected = "A" + replacement + "Z" + replacement;
    EXPECT_EQ(utf8::replace_invalid(input), expected);
    EXPECT_EQ(utf8::replace_invalid(input, U'?'), "A?Z?");
    EXPECT_EQ(utf8::replace_invalid(std::string_view(input)), expected);
    std::string output;
    utf8::replace_invalid(input.begin(), input.end(), std::back_inserter(output));
    EXPECT_EQ(output, expected);
    EXPECT_TRUE(utf8::is_valid(output));
}

TEST(UtfCppReplacementTests, PreservesValidTextIncludingBomNulAndCombiningSequences)
{
    for (const auto &value : validEncodings())
    {
        SCOPED_TRACE(value.m_name);
        EXPECT_EQ(utf8::replace_invalid(value.m_utf8), value.m_utf8);
    }
}

TEST(UtfCppConversionTests, HandlesLongMixedTextWithIndependentExpectedCodeUnits)
{
    const auto &value = mixedEncoding();
    std::string input;
    std::u16string expected16;
    std::u32string expected32;
    for (int count = 0; count < 4096; ++count)
    {
        input += value.m_utf8;
        expected16 += value.m_utf16;
        expected32 += value.m_utf32;
    }
    EXPECT_TRUE(utf8::is_valid(input));
    EXPECT_EQ(utf8::distance(input.begin(), input.end()), 6 * 4096);
    EXPECT_EQ(utf8::utf8to16(input), expected16);
    EXPECT_EQ(utf8::utf8to32(input), expected32);
    EXPECT_EQ(utf8::utf16to8(expected16), input);
    EXPECT_EQ(utf8::utf32to8(expected32), input);
}
