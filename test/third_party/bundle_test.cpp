#include <gtest/gtest.h>

#include <string>
#include <cstdint>

#include <bundle.h>


namespace
{

    std::string makeRepeatedText(size_t repetitions)
    {
        const std::string pattern = "bundle LZIP round-trip: cloud storage\n";
        std::string payload;
        payload.reserve(pattern.size() * repetitions);

        for (size_t i = 0; i < repetitions; ++i)
        {
            payload += pattern;
        }

        return payload;
    }

    std::string makeBinaryPayload(size_t repetitions)
    {
        const unsigned char pattern[] = {0x00, 0x01, 0x7f, 0x80, 0xfe, 0xff, 0x00, 0xa5};
        std::string payload;
        payload.reserve(sizeof(pattern) * repetitions);

        for (size_t i = 0; i < repetitions; ++i)
        {
            for (unsigned char byte : pattern)
            {
                payload.push_back(static_cast<char>(byte));
            }
        }

        return payload;
    }

    std::string makeDeterministicNoise(size_t size)
    {
        std::string payload;
        payload.reserve(size);

        uint32_t state = 0x6d2b79f5U;
        for (size_t i = 0; i < size; ++i)
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            payload.push_back(static_cast<char>(state & 0xffU));
        }

        return payload;
    }

} // namespace


TEST(BundleStreamTests, EmptyInputRemainsEmpty)
{
    const std::string input;

    const std::string packed = bundle::pack(bundle::LZIP, input);
    EXPECT_TRUE(packed.empty());
    EXPECT_EQ(bundle::unpack(packed), input);
}

TEST(BundleStreamTests, LzipRoundTripsRepeatedText)
{
    const std::string input = makeRepeatedText(2048);
    ASSERT_FALSE(input.empty());

    const std::string packed = bundle::pack(bundle::LZIP, input);

    ASSERT_FALSE(packed.empty());
    EXPECT_TRUE(bundle::is_packed(packed));
    EXPECT_EQ(bundle::unpack(packed), input);
}

TEST(BundleStreamTests, LzipRoundTripsBinaryDataWithNulsAndHighBytes)
{
    const std::string input = makeBinaryPayload(4096);
    ASSERT_FALSE(input.empty());

    const std::string packed = bundle::pack(bundle::LZIP, input);

    ASSERT_TRUE(bundle::is_packed(packed));
    EXPECT_EQ(bundle::unpack(packed), input);
}

TEST(BundleStreamTests, OutputParameterApiReportsSuccessfulCompression)
{
    const std::string input = makeRepeatedText(512);
    std::string packed;

    ASSERT_TRUE(bundle::pack(bundle::LZIP, packed, input));
    EXPECT_TRUE(bundle::is_packed(packed));

    std::string output;
    ASSERT_TRUE(bundle::unpack(output, packed));
    EXPECT_EQ(output, input);
}

TEST(BundleStreamTests, LzipPreservesHighEntropyDataWhenCompressionIsUnhelpful)
{
    const std::string input = makeDeterministicNoise(32 * 1024);
    std::string packed;

    const bool compressed = bundle::pack(bundle::LZIP, packed, input);

    if (compressed)
    {
        EXPECT_TRUE(bundle::is_packed(packed));
        EXPECT_EQ(bundle::unpack(packed), input);
    }
    else
    {
        EXPECT_FALSE(bundle::is_packed(packed));
        EXPECT_EQ(packed, input);

        std::string output;
        EXPECT_FALSE(bundle::unpack(output, packed));
        EXPECT_EQ(output, input);
    }
}

TEST(BundleStreamTests, LzipRoundTripsPayloadsAcrossSmallBoundarySizes)
{
    const size_t sizes[] = {1, 2, 4, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256, 257, 4096};

    for (size_t size : sizes)
    {
        SCOPED_TRACE(size);
        const std::string input = makeDeterministicNoise(size);
        const std::string packed = bundle::pack(bundle::LZIP, input);
        EXPECT_EQ(bundle::unpack(packed), input);
    }
}

TEST(BundleStreamTests, PlainTextIsNotMisidentifiedAsPackedData)
{
    const std::string input = "this is ordinary text, not a bundle stream";
    std::string output = "previous output";

    EXPECT_FALSE(bundle::is_packed(input));
    EXPECT_TRUE(bundle::is_unpacked(input));
    EXPECT_FALSE(bundle::unpack(output, input));
    EXPECT_EQ(output, input);
}
