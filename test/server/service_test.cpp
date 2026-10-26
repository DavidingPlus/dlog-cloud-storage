#include "service_test_util.h"

using namespace service_test;


// 私有辅助函数直接测试；页面和路由通过真实 libevent HTTP 请求测试，不引入 mock 机制。
namespace
{
    constexpr SizeCase kSizeCases[] = {
        {0, "0.00 B"},
        {1, "1.00 B"},
        {1023, "1023.00 B"},
        {1024, "1.00 KB"},
        {1025, "1.00 KB"},
        {1536, "1.50 KB"},
        {1029, "1.00 KB"},
        {1030, "1.01 KB"},
        {1048575, "1024.00 KB"},
        {1048576, "1.00 MB"},
        {1572864, "1.50 MB"},
        {1073741823, "1024.00 MB"},
        {1073741824, "1.00 GB"},
        {1610612736, "1.50 GB"},
        {1099511627776ULL, "1024.00 GB"},
        {std::numeric_limits<uint64_t>::max(), "17179869184.00 GB"}};
}


// 格式化接口：单位阈值、四舍五入、零值与 uint64_t 上界。
TEST_P(ServiceSizeTests, FormatsBytesWithTwoDecimalsAndBinaryUnits)
{
    const auto &test = GetParam();
    SCOPED_TRACE(std::to_string(test.m_bytes));
    EXPECT_EQ(Access::formatFileSize(test.m_bytes), test.m_expected);
}

INSTANTIATE_TEST_SUITE_P(Boundaries, ServiceSizeTests, ::testing::ValuesIn(kSizeCases));

TEST(ServiceSizeStandaloneTests, ConcurrentCallsReturnIndependentStrings)
{
    std::vector<std::future<std::string>> results;
    for (int i = 0; i < 16; ++i)
        results.emplace_back(std::async(std::launch::async, [i]
                                        { return Access::formatFileSize(1024ULL * (1 + i)); }));
    for (int i = 0; i < 16; ++i) EXPECT_EQ(results[i].get(), std::to_string(1 + i) + ".00 KB");
}

TEST_F(ServiceTimeTests, FormatsCalendarFieldsWithZeroPadding)
{
    EXPECT_EQ(Access::formatFileTime(FileTimeAt(946684800)), "2000-01-01 00:00:00");
    EXPECT_EQ(Access::formatFileTime(FileTimeAt(951825906)), "2000-02-29 12:05:06");
}

TEST_F(ServiceTimeTests, HandlesEpochAndNegativeFractionalTimestamps)
{
    EXPECT_EQ(Access::formatFileTime(FileTimeAt(0)), "1970-01-01 00:00:00");
    EXPECT_EQ(Access::formatFileTime(FileTimeAt(-1, std::chrono::milliseconds(750))), "1969-12-31 23:59:59");
    EXPECT_EQ(Access::formatFileTime(FileTimeAt(-86400)), "1969-12-31 00:00:00");
}

TEST_F(ServiceTimeTests, HandlesYearRolloverAndThe2038Boundary)
{
    EXPECT_EQ(Access::formatFileTime(FileTimeAt(946684799)), "1999-12-31 23:59:59");
    EXPECT_EQ(Access::formatFileTime(FileTimeAt(2147483647)), "2038-01-19 03:14:07");
    EXPECT_EQ(Access::formatFileTime(FileTimeAt(2147483648LL)), "2038-01-19 03:14:08");
}

TEST_F(ServiceTimeTests, DiscardsSubsecondsWithoutRoundingUp)
{
    EXPECT_EQ(Access::formatFileTime(FileTimeAt(946684800, std::chrono::milliseconds(250))), "2000-01-01 00:00:00");
    EXPECT_EQ(Access::formatFileTime(FileTimeAt(946684800, std::chrono::milliseconds(750))), "2000-01-01 00:00:00");
}

TEST_F(ServiceTimeTests, UsesTheServersLocalTimezone)
{
    const ScopedTimezone localZone("UTC-8"); // POSIX 记法：UTC-8 表示东八区。
    EXPECT_EQ(Access::formatFileTime(FileTimeAt(946684800)), "2000-01-01 08:00:00");
}

TEST_F(ServiceTimeTests, AppliesDaylightSavingTransitions)
{
    const ScopedTimezone localZone("EST5EDT,M3.2.0/2,M11.1.0/2");
    EXPECT_EQ(Access::formatFileTime(FileTimeAt(1615705199)), "2021-03-14 01:59:59");
    EXPECT_EQ(Access::formatFileTime(FileTimeAt(1615705200)), "2021-03-14 03:00:00");
}

TEST_F(ServiceTimeTests, ConcurrentCallsDoNotShareTheCalendarBuffer)
{
    std::vector<std::future<std::string>> results;
    const auto first = FileTimeAt(946684800);
    const auto second = FileTimeAt(951825906);
    for (int i = 0; i < 24; ++i)
        results.emplace_back(std::async(std::launch::async, [=]
                                        { return Access::formatFileTime(0 == i % 2 ? first : second); }));
    for (int i = 0; i < 24; ++i)
        EXPECT_EQ(results[i].get(), 0 == i % 2 ? "2000-01-01 00:00:00" : "2000-02-29 12:05:06");
}

TEST_F(ServiceConstructorTests, CapturesListeningConfiguration)
{
    m_config["download_prefix"] = "/files/";
    applyConfig();
    server::Service service;
    EXPECT_EQ(Access::ip(service), "127.0.0.2");
    EXPECT_EQ(Access::port(service), 18081);
    EXPECT_EQ(Access::prefix(service), "/files/");
}

TEST_F(ServiceConstructorTests, LoadsOwnedMetadataFromDisk)
{
    const auto file = makeInfo("persisted.txt", 8193);
    writeMetadata({file});
    server::Service service;
    const auto loaded = Access::data(service).getAll();
    ASSERT_EQ(loaded.size(), 1U);
    EXPECT_EQ(loaded[0].m_absoluteFilePath, file.m_absoluteFilePath);
    EXPECT_EQ(loaded[0].m_fileSize, file.m_fileSize);
    EXPECT_EQ(loaded[0].m_mtime, file.m_mtime);
    EXPECT_EQ(loaded[0].m_url, file.m_url);
}

TEST_F(ServiceConstructorTests, AcceptsAnEmptyMetadataArray)
{
    server::Service service;
    EXPECT_TRUE(Access::data(service).getAll().empty());
}

TEST_F(ServiceConstructorTests, PropagatesMissingMetadataErrors)
{
    fs::remove(m_metadataFile);
    EXPECT_THROW(server::Service{}, fs::filesystem_error);
}

TEST_F(ServiceConstructorTests, PropagatesMalformedMetadataErrors)
{
    for (const auto *content : {"{", "{}", "[{}]"})
    {
        SCOPED_TRACE(content);
        utils::FileUtil(m_metadataFile).setContent(content);
        EXPECT_THROW(server::Service{}, std::invalid_argument);
    }
}

TEST_F(ServiceConstructorTests, ConstructionAndDestructionDoNotRewriteMetadata)
{
    writeMetadata({makeInfo()});
    const auto content = utils::FileUtil(m_metadataFile).getContent();
    fs::last_write_time(m_metadataFile, FileTimeAt(946684800));
    const auto modifiedTime = fs::last_write_time(m_metadataFile);
    {
        server::Service service;
    }
    EXPECT_EQ(utils::FileUtil(m_metadataFile).getContent(), content);
    EXPECT_EQ(fs::last_write_time(m_metadataFile), modifiedTime);
}

TEST_F(ServiceConstructorTests, KeepsCapturedConfigurationAndMetadataAfterConfigReload)
{
    server::Service first;
    const auto firstFile = makeInfo("first.txt");
    const auto otherMetadata = m_directory / "second.json";
    utils::FileUtil(otherMetadata).setContent("[]");
    m_config["metadata_file"] = otherMetadata.string();
    m_config["server_ip"] = "127.0.0.3";
    m_config["server_port"] = 19090;
    m_config["download_prefix"] = "/files/";
    applyConfig();
    server::Service second;
    Access::data(first).insert(firstFile);

    EXPECT_EQ(Access::ip(first), "127.0.0.2");
    EXPECT_EQ(Access::port(first), 18081);
    EXPECT_EQ(Access::prefix(first), "/download/");
    EXPECT_EQ(Access::ip(second), "127.0.0.3");
    EXPECT_EQ(Access::port(second), 19090);
    EXPECT_EQ(Access::prefix(second), "/files/");
    EXPECT_TRUE(Access::data(second).getAll().empty());
    EXPECT_EQ(utils::FileUtil(otherMetadata).getContent(), "[]");
    EXPECT_NE(utils::FileUtil(m_metadataFile).getContent().find("first.txt"), std::string::npos);
}

TEST_F(ServiceHtmlTests, EmptyListHasAHeadingAndBalancedContainer)
{
    EXPECT_EQ(Access::fileListHtml({}), "<div class='file-list'><h3>已上传文件</h3></div>");
}

TEST_F(ServiceHtmlTests, IncludesTheNameStorageSizeTimeAndDownloadLink)
{
    const ScopedTimezone timezone("UTC0");
    const auto html = Access::fileListHtml({makeInfo("report.txt")});
    EXPECT_NE(html.find("📄report.txt"), std::string::npos);
    EXPECT_NE(html.find("未压缩存储"), std::string::npos);
    EXPECT_NE(html.find("存储大小：1.50 KB"), std::string::npos);
    EXPECT_NE(html.find("2000-01-01 00:00:00"), std::string::npos);
    EXPECT_NE(html.find("window.location='/download/report.txt'"), std::string::npos);
    EXPECT_EQ(Count(html, "class='file-item'"), 1U);
}

TEST_F(ServiceHtmlTests, IdentifiesCompressedStorageUsingTheConfiguredDirectory)
{
    auto file = makeInfo("report.txt", 1024, true);
    file.m_absoluteFilePath = file.m_absoluteFilePath.parent_path() / "unused/.././report.txt";
    const auto html = Access::fileListHtml({file});
    EXPECT_NE(html.find("<span class='file-type'>压缩存储</span>"), std::string::npos);
    EXPECT_EQ(html.find("未压缩存储"), std::string::npos);
}

TEST_F(ServiceHtmlTests, DoesNotInferStorageFromTheFilenameOrDirectorySubstring)
{
    auto first = makeInfo("compressed-deep-low.txt");
    auto second = makeInfo("report.txt");
    second.m_absoluteFilePath = m_directory / "compressed-backup" / "report.txt";
    const auto html = Access::fileListHtml({first, second});
    EXPECT_EQ(Count(html, "<span class='file-type'>未压缩存储</span>"), 2U);
}

TEST_F(ServiceHtmlTests, EscapesHtmlAndEncodesTheDownloadTarget)
{
    const auto file = makeInfo("<img src=x onerror='alert(1)'>&\".txt");
    const auto html = Access::fileListHtml({file});
    EXPECT_NE(html.find("&lt;img src=x onerror=&#39;alert(1)&#39;&gt;&amp;&quot;.txt"), std::string::npos);
    EXPECT_EQ(html.find("<img"), std::string::npos);
    EXPECT_NE(html.find("/download/%3Cimg%20src%3Dx%20onerror%3D%27alert%281%29%27%3E%26%22.txt"), std::string::npos);
}

TEST_F(ServiceHtmlTests, PreservesUnicodeInTextAndEncodesItsUtf8BytesInLinks)
{
    const auto html = Access::fileListHtml({makeInfo(u8"中文.txt")});
    EXPECT_NE(html.find(u8"📄中文.txt"), std::string::npos);
    EXPECT_NE(html.find("/download/%E4%B8%AD%E6%96%87.txt"), std::string::npos);
}

TEST_F(ServiceHtmlTests, UsesTheMetadataUrlInsteadOfRebuildingItFromTheFilename)
{
    auto file = makeInfo("visible.txt");
    file.m_url = "/custom/a+b %?#.txt";
    const auto html = Access::fileListHtml({file});
    EXPECT_NE(html.find("📄visible.txt"), std::string::npos);
    EXPECT_NE(html.find("window.location='/custom/a%2Bb%20%25%3F%23.txt'"), std::string::npos);
    EXPECT_EQ(html.find("/download/visible.txt"), std::string::npos);
}

TEST_F(ServiceHtmlTests, PreservesInputOrderAndCreatesOneRowPerFile)
{
    const auto html = Access::fileListHtml({makeInfo("z.txt"), makeInfo("a.txt", 0, true), makeInfo("m.txt")});
    EXPECT_LT(html.find("📄z.txt"), html.find("📄a.txt"));
    EXPECT_LT(html.find("📄a.txt"), html.find("📄m.txt"));
    EXPECT_EQ(Count(html, "class='file-item'"), 3U);
    EXPECT_EQ(Count(html, "<button "), 3U);
    EXPECT_NE(html.find("存储大小：0.00 B"), std::string::npos);
}

TEST_F(ServiceHtmlTests, FormatsMetadataWithoutReadingTheUnderlyingFile)
{
    const auto file = makeInfo("not-on-disk.txt");
    ASSERT_FALSE(fs::exists(file.m_absoluteFilePath));
    EXPECT_NO_THROW(Access::fileListHtml({file}));
}

TEST_F(ServiceHtmlTests, RepeatedRenderingDoesNotMutateTheInput)
{
    const std::vector<server::FileMetadata> files{makeInfo("a.txt"), makeInfo("b.txt", 0, true)};
    const auto original = files;
    EXPECT_EQ(Access::fileListHtml(files), Access::fileListHtml(files));
    for (size_t i = 0; i < files.size(); ++i)
    {
        EXPECT_EQ(files[i].m_mtime, original[i].m_mtime);
        EXPECT_EQ(files[i].m_fileSize, original[i].m_fileSize);
        EXPECT_EQ(files[i].m_absoluteFilePath, original[i].m_absoluteFilePath);
        EXPECT_EQ(files[i].m_url, original[i].m_url);
    }
}

TEST_F(ServiceETagTests, ContainsTheQuotedFilenameSizeAndRawFileClockCount)
{
    EXPECT_EQ(Access::etag(ETagInfo()), "\"a.txt-1024-123456\"");
    EXPECT_EQ(Access::etag(ETagInfo("empty.txt", 0, -7)), "\"empty.txt-0--7\"");
}

TEST_F(ServiceETagTests, UsesOnlyTheBasenameAndIgnoresTheUrl)
{
    auto first = ETagInfo();
    auto second = first;
    second.m_absoluteFilePath = "/other/directory/a.txt";
    second.m_url = "/different/resource";
    EXPECT_EQ(Access::etag(first), Access::etag(second));
}

TEST_F(ServiceETagTests, ChangesWhenAnyVersionFieldChanges)
{
    const auto original = ETagInfo();
    const auto expected = Access::etag(original);
    auto renamed = original;
    renamed.m_absoluteFilePath = "/storage/b.txt";
    auto resized = original;
    ++resized.m_fileSize;
    auto modified = original;
    modified.m_mtime += fs::file_time_type::duration(1);
    EXPECT_NE(Access::etag(renamed), expected);
    EXPECT_NE(Access::etag(resized), expected);
    EXPECT_NE(Access::etag(modified), expected);
    EXPECT_EQ(Access::etag(original), expected);
}

TEST_F(ServiceETagTests, PercentEncodesUnsafeFilenameBytes)
{
    const std::vector<std::pair<std::string, std::string>> cases{
        {"a b.txt", "a%20b.txt"}, {"quote\"\\\r\n%.txt", "quote%22%5C%0D%0A%25.txt"}, {u8"中文.txt", "%E4%B8%AD%E6%96%87.txt"}, {std::string("nul\0byte.txt", 12), "nul%00byte.txt"}, {std::string("raw") + static_cast<char>(0xFF) + ".txt", "raw%FF.txt"}, {"safe-_.~AZaz09.txt", "safe-_.~AZaz09.txt"}};
    for (const auto &test : cases)
    {
        SCOPED_TRACE(test.second);
        const auto tag = Access::etag(ETagInfo(test.first));
        EXPECT_EQ(tag, "\"" + test.second + "-1024-123456\"");
        EXPECT_TRUE(std::all_of(tag.begin(), tag.end(), [](unsigned char ch)
                                { return ch >= 0x21 && ch <= 0x7E; }));
    }
}

TEST_F(ServiceETagTests, HandlesAnEmptyBasename)
{
    auto info = ETagInfo();
    info.m_absoluteFilePath = "/storage/";
    EXPECT_EQ(Access::etag(info), "\"-1024-123456\"");
}

TEST_F(ServiceETagTests, DoesNotTruncateLargeSizesOrExtremeRawTimestamps)
{
    for (const auto ticks : {fs::file_time_type::duration::min().count(), fs::file_time_type::duration::max().count()})
    {
        const auto info = ETagInfo("large.txt", std::numeric_limits<size_t>::max(), ticks);
        EXPECT_EQ(Access::etag(info), "\"large.txt-" + std::to_string(std::numeric_limits<size_t>::max()) + "-" + std::to_string(ticks) + "\"");
    }
}

TEST_F(ServiceETagTests, IsIndependentOfTheTimezone)
{
    const auto info = ETagInfo();
    const auto expected = Access::etag(info);
    const ScopedTimezone timezone("UTC-8");
    EXPECT_EQ(Access::etag(info), expected);
}

TEST_F(ServiceETagTests, ConcurrentCallsAreStableAndDoNotShareOutputBuffers)
{
    const auto info = ETagInfo();
    std::vector<std::future<std::string>> results;
    for (int i = 0; i < 16; ++i)
        results.emplace_back(std::async(std::launch::async, [info]
                                        { return Access::etag(info); }));
    for (auto &result : results) EXPECT_EQ(result.get(), "\"a.txt-1024-123456\"");
}
