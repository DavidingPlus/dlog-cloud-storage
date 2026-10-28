#include "service_test_util.h"

#include <base64.h>

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

TEST_F(ServiceHttpTests, ReturnsAnHtmlPageForEmptyMetadata)
{
    server::Service service;
    LocalHttpServer http(service);
    const auto response = http.get("/");
    EXPECT_EQ(response.m_code, HTTP_OK);
    EXPECT_EQ(response.m_contentType, "text/html;charset=utf-8");
    EXPECT_EQ(response.m_body, "<!DOCTYPE html>\n<html><body><div class='file-list'><h3>已上传文件</h3></div><script>const backend = 'http://127.0.0.2:18081';</script></body></html>\n");
}

TEST_F(ServiceHttpTests, IncludesEveryStoredFileWithoutDependingOnMapOrder)
{
    const ScopedTimezone timezone("UTC0");
    writeMetadata({makeInfo("ordinary.txt"), makeInfo("archived.txt", 1024, true)});
    server::Service service;
    LocalHttpServer http(service);
    const auto response = http.get("/");
    ASSERT_EQ(response.m_code, HTTP_OK);
    EXPECT_EQ(Count(response.m_body, "class='file-item'"), 2U);
    EXPECT_NE(response.m_body.find("📄ordinary.txt"), std::string::npos);
    EXPECT_NE(response.m_body.find("📄archived.txt"), std::string::npos);
    EXPECT_NE(response.m_body.find("未压缩存储"), std::string::npos);
    EXPECT_NE(response.m_body.find("<span class='file-type'>压缩存储</span>"), std::string::npos);
    EXPECT_NE(response.m_body.find("2000-01-01 00:00:00"), std::string::npos);
    EXPECT_EQ(response.m_body.find("{{FILE_LIST}}"), std::string::npos);
    EXPECT_EQ(response.m_body.find("{{BACKEND_URL}}"), std::string::npos);
}

TEST_F(ServiceHttpTests, ReplacesEveryOccurrenceOfBothTemplatePlaceholders)
{
    writeTemplate("{{FILE_LIST}}|{{BACKEND_URL}}|{{FILE_LIST}}|{{BACKEND_URL}}");
    server::Service service;
    LocalHttpServer http(service);
    const auto body = http.get("/").m_body;
    EXPECT_EQ(Count(body, "<h3>已上传文件</h3>"), 2U);
    EXPECT_EQ(Count(body, "http://127.0.0.2:18081"), 2U);
    EXPECT_EQ(body.find("{{"), std::string::npos);
}

TEST_F(ServiceHttpTests, PreservesTemplatesWithoutPlaceholders)
{
    const std::string page = "<!DOCTYPE html>\r\n<p>静态内容 $&</p>\n";
    writeTemplate(page);
    server::Service service;
    LocalHttpServer http(service);
    EXPECT_EQ(http.get("/").m_body, page);
}

TEST_F(ServiceHttpTests, PreservesEmbeddedNulsInTheTemplateBody)
{
    const std::string page("head\0tail", 9);
    writeTemplate(page);
    server::Service service;
    LocalHttpServer http(service);
    EXPECT_EQ(http.get("/").m_body, page);
}

TEST_F(ServiceHttpTests, AcceptsAnEmptyTemplate)
{
    writeTemplate("");
    server::Service service;
    LocalHttpServer http(service);
    const auto response = http.get("/");
    EXPECT_EQ(response.m_code, HTTP_OK);
    EXPECT_TRUE(response.m_body.empty());
    EXPECT_EQ(response.m_contentType, "text/html;charset=utf-8");
}

TEST_F(ServiceHttpTests, ReadsTheTemplateBesideTheExecutableAfterWorkingDirectoryChanges)
{
    fs::create_directories(m_directory / "web");
    utils::FileUtil(m_directory / "web/template.html").setContent("wrong template from current directory");
    fs::current_path(m_directory);
    server::Service service;
    LocalHttpServer http(service);
    const auto body = http.get("/").m_body;
    EXPECT_NE(body.find("<!DOCTYPE html>"), std::string::npos);
    EXPECT_EQ(body.find("wrong template"), std::string::npos);
}

TEST_F(ServiceHttpTests, UsesTheServiceMetadataSnapshotInsteadOfReloadingTheFilePerRequest)
{
    writeMetadata({makeInfo("before.txt")});
    server::Service service;
    writeMetadata({makeInfo("after.txt")});
    LocalHttpServer http(service);
    const auto body = http.get("/").m_body;
    EXPECT_NE(body.find("📄before.txt"), std::string::npos);
    EXPECT_EQ(body.find("📄after.txt"), std::string::npos);
}

TEST_F(ServiceHttpTests, UsesTheServicePassedAsCallbackContext)
{
    server::Service first;
    server::Service second;
    Access::data(first).insert(makeInfo("first.txt"));
    Access::data(second).insert(makeInfo("second.txt"));
    LocalHttpServer firstHttp(first);
    LocalHttpServer secondHttp(second);
    const auto firstBody = firstHttp.get("/").m_body;
    const auto secondBody = secondHttp.get("/").m_body;
    EXPECT_NE(firstBody.find("📄first.txt"), std::string::npos);
    EXPECT_EQ(firstBody.find("📄second.txt"), std::string::npos);
    EXPECT_NE(secondBody.find("📄second.txt"), std::string::npos);
    EXPECT_EQ(secondBody.find("📄first.txt"), std::string::npos);
}

TEST_F(ServiceHttpTests, ReadsTheLatestTemplateOnEachRequest)
{
    server::Service service;
    LocalHttpServer http(service);
    writeTemplate("first version");
    EXPECT_EQ(http.get("/").m_body, "first version");
    writeTemplate("second version");
    EXPECT_EQ(http.get("/").m_body, "second version");
}

TEST_F(ServiceHttpTests, RendersTheRealApplicationTemplate)
{
    writeTemplate(m_originalTemplate);
    writeMetadata({makeInfo(u8"中文 &.txt")});
    server::Service service;
    LocalHttpServer http(service);
    const auto response = http.get("/");
    ASSERT_EQ(response.m_code, HTTP_OK);
    EXPECT_NE(response.m_body.find("<!DOCTYPE html>"), std::string::npos);
    EXPECT_NE(response.m_body.find("value=\"uncompressed\" checked"), std::string::npos);
    EXPECT_NE(response.m_body.find("value=\"compressed\""), std::string::npos);
    EXPECT_NE(response.m_body.find(u8"📄中文 &amp;.txt"), std::string::npos);
    EXPECT_NE(response.m_body.find("/download/%E4%B8%AD%E6%96%87%20%26.txt"), std::string::npos);
    EXPECT_EQ(response.m_body.find("{{FILE_LIST}}"), std::string::npos);
    EXPECT_EQ(response.m_body.find("{{BACKEND_URL}}"), std::string::npos);
}

TEST_F(ServiceHttpTests, PropagatesMissingTemplateErrorsBeforeSendingAReply)
{
    server::Service service;
    auto request = MakeRequest();
    fs::remove(m_templateFile);
    EXPECT_THROW(Access::showFileList(request.get(), service), fs::filesystem_error);
    EXPECT_EQ(evbuffer_get_length(evhttp_request_get_output_buffer(request.get())), 0U);
}

TEST_F(ServiceHttpTests, RejectsADirectoryAtTheTemplatePath)
{
    server::Service service;
    auto request = MakeRequest();
    fs::remove(m_templateFile);
    fs::create_directory(m_templateFile);
    EXPECT_THROW(Access::showFileList(request.get(), service), fs::filesystem_error);
}

TEST_F(ServiceHttpTests, PropagatesUnreadableTemplateErrors)
{
    server::Service service;
    auto request = MakeRequest();
    fs::permissions(m_templateFile, fs::perms::none);
    std::ifstream probe(m_templateFile);
    if (probe.is_open()) GTEST_SKIP() << "The current user can read files without read permission";
    EXPECT_THROW(Access::showFileList(request.get(), service), std::ios_base::failure);
}

// 以下两个回归用例验证文件名按字面显示，不应成为模板替换指令。
TEST_F(ServiceHttpTests, PreservesDollarSequencesInFilenames)
{
    writeMetadata({makeInfo("price$&$1$$$`$'.txt")});
    server::Service service;
    LocalHttpServer http(service);
    const auto body = http.get("/").m_body;
    EXPECT_NE(body.find("📄price$&amp;$1$$$`$&#39;.txt"), std::string::npos);
    EXPECT_NE(body.find("/download/price%24%26%241%24%24%24%60%24%27.txt"), std::string::npos);
}

TEST_F(ServiceHttpTests, DoesNotExpandBackendPlaceholdersInsideFilenames)
{
    writeMetadata({makeInfo("{{BACKEND_URL}}.txt")});
    server::Service service;
    LocalHttpServer http(service);
    const auto body = http.get("/").m_body;
    EXPECT_NE(body.find("📄{{BACKEND_URL}}.txt"), std::string::npos);
    EXPECT_EQ(body.find("📄http://"), std::string::npos);
}

TEST_F(ServiceHttpTests, DoesNotRecursivelyExpandFileListPlaceholdersInsideFilenames)
{
    writeMetadata({makeInfo("{{FILE_LIST}}.txt")});
    server::Service service;
    LocalHttpServer http(service);
    const auto body = http.get("/").m_body;
    EXPECT_NE(body.find("📄{{FILE_LIST}}.txt"), std::string::npos);
    EXPECT_EQ(Count(body, "class='file-item'"), 1U);
}

TEST_F(ServiceHttpTests, UploadsTextBinaryAndEmptyFilesAndRestoresTheirMetadata)
{
    server::Service service;
    LocalHttpServer http(service);
    const std::vector<std::pair<std::string, std::string>> files{
        {"hello.txt", "hello upload\n"},
        {u8"中文 +.bin", std::string("a\0\xffz", 4)},
        {"empty.txt", ""}};
    for (const auto &file : files)
    {
        SCOPED_TRACE(file.first);
        const auto response = http.post("/upload", {{"FileName", base64_encode(file.first)}, {"StorageType", "uncompressed"}}, file.second);
        ASSERT_EQ(response.m_code, 201);
        EXPECT_EQ(response.m_contentType, "application/json;charset=utf-8");
        const auto url = "/download/" + file.first;
        EXPECT_EQ(utils::JsonUtil::UnSerialize(response.m_body)["url"].asString(), utils::urlEncode(url));
        const auto path = server::Config::GetInstance().getUncompressedFilesDirPath() / file.first;
        EXPECT_EQ(utils::FileUtil(path).getContent(), file.second);
        const auto info = Access::data(service).getFileMetaDataByUrl(url);
        EXPECT_EQ(info.m_absoluteFilePath, path);
        EXPECT_EQ(info.m_fileSize, file.second.size());
        EXPECT_EQ(info.m_mtime, fs::last_write_time(path));
        server::DataManager restored;
        EXPECT_EQ(restored.getFileMetaDataByUrl(url).m_absoluteFilePath, path);
    }
    EXPECT_EQ(Count(http.get("/").m_body, "class='file-item'"), files.size());
}

TEST_F(ServiceHttpTests, RejectsInvalidUploadParametersBeforeCreatingFiles)
{
    server::Service service;
    LocalHttpServer http(service);
    EXPECT_EQ(http.get("/upload").m_code, HTTP_BADMETHOD);
    for (const char *type : {"unknown", ""})
        EXPECT_EQ(http.post("/upload", {{"FileName", "YS50eHQ="}, {"StorageType", type}}, "data").m_code, HTTP_BADREQUEST);
    for (const char *name : {"", "!m9v", "Z"})
        EXPECT_EQ(http.post("/upload", {{"FileName", name}, {"StorageType", "uncompressed"}}, "data").m_code, HTTP_BADREQUEST);
    // TODO: 文件名合法性校验补齐后，再恢复目录跳转、路径分隔符、控制字符等非法名称用例。
    EXPECT_TRUE(Access::data(service).getAll().empty());
    EXPECT_FALSE(fs::exists(server::Config::GetInstance().getUncompressedFilesDirPath()));
}

// 分别验证两个字段都缺失、只缺 StorageType、只缺 FileName；应在构造字符串和写文件前返回 400。
TEST_F(ServiceHttpTests, RejectsMissingUploadHeadersBeforeCreatingFiles)
{
    server::Service service;
    LocalHttpServer http(service);
    EXPECT_EQ(http.post("/upload", {}, "data").m_code, HTTP_BADREQUEST);
    EXPECT_EQ(http.post("/upload", {{"FileName", "YS50eHQ="}}, "data").m_code, HTTP_BADREQUEST);
    EXPECT_EQ(http.post("/upload", {{"StorageType", "uncompressed"}}, "data").m_code, HTTP_BADREQUEST);
    EXPECT_TRUE(Access::data(service).getAll().empty());
    EXPECT_FALSE(fs::exists(server::Config::GetInstance().getUncompressedFilesDirPath()));
}

TEST_F(ServiceHttpTests, UploadsCompressedFilesAndRecordsTheirStoredSize)
{
    server::Service service;
    LocalHttpServer http(service);
    const std::vector<std::pair<std::string, std::string>> files{
        {u8"压缩 +.txt", std::string(32768, 'A')},
        {"binary.bin", std::string("a\0\xffz", 4)},
        {"empty.txt", ""}};
    for (const auto &file : files)
    {
        SCOPED_TRACE(file.first);
        const auto response = http.post("/upload", {{"FileName", base64_encode(file.first)}, {"StorageType", "compressed"}}, file.second);
        ASSERT_EQ(response.m_code, 201);
        const auto path = server::Config::GetInstance().getCompressedFilesDirPath() / file.first;
        const auto stored = utils::FileUtil(path).getContent();
        EXPECT_EQ(utils::FileUtil::UnCompress(stored), file.second);
        const auto url = "/download/" + file.first;
        const auto info = Access::data(service).getFileMetaDataByUrl(url);
        EXPECT_EQ(info.m_absoluteFilePath, path);
        EXPECT_EQ(info.m_fileSize, stored.size());
        EXPECT_EQ(info.m_mtime, fs::last_write_time(path));
        server::DataManager restored;
        EXPECT_EQ(restored.getFileMetaDataByUrl(url).m_fileSize, stored.size());
        EXPECT_EQ(utils::JsonUtil::UnSerialize(response.m_body)["url"].asString(), utils::urlEncode(url));
        // 另一种策略使用同一下载 URL，也必须拒绝，保留原压缩文件。
        EXPECT_EQ(http.post("/upload", {{"FileName", base64_encode(file.first)}, {"StorageType", "uncompressed"}}, "replacement").m_code, 409);
        EXPECT_EQ(utils::FileUtil(path).getContent(), stored);
    }
    const auto path = server::Config::GetInstance().getCompressedFilesDirPath() / files.front().first;
    EXPECT_LT(utils::FileUtil(path).fileSize(), files.front().second.size());
}

TEST_F(ServiceHttpTests, RejectsUploadConflictsAndPreservesExistingData)
{
    server::Service service;
    LocalHttpServer http(service);
    const std::vector<std::pair<std::string, std::string>> headers{{"FileName", "YS50eHQ="}, {"StorageType", "uncompressed"}};
    ASSERT_EQ(http.post("/upload", headers, "original").m_code, 201);
    const auto metadata = utils::FileUtil(m_metadataFile).getContent();
    const auto path = server::Config::GetInstance().getUncompressedFilesDirPath() / "a.txt";
    EXPECT_EQ(http.post("/upload", headers, "replacement").m_code, 409);
    EXPECT_EQ(http.post("/upload", {{"FileName", "YS50eHQ="}, {"StorageType", "compressed"}}, "replacement").m_code, 409);
    EXPECT_EQ(utils::FileUtil(path).getContent(), "original");
    EXPECT_EQ(utils::FileUtil(m_metadataFile).getContent(), metadata);
    // 即使旧文件丢失，已有 URL 也不能被新上传覆盖。
    fs::remove(path);
    EXPECT_EQ(http.post("/upload", headers, "replacement").m_code, 409);
    EXPECT_FALSE(fs::exists(path));
    // 未登记元数据的磁盘文件也须保留。
    const auto occupied = path.parent_path() / "occupied.txt";
    utils::FileUtil(occupied).setContent("keep");
    EXPECT_EQ(http.post("/upload", {{"FileName", base64_encode(std::string("occupied.txt"))}, {"StorageType", "uncompressed"}}, "replacement").m_code, 409);
    EXPECT_EQ(utils::FileUtil(occupied).getContent(), "keep");
    EXPECT_EQ(Access::data(service).getAll().size(), 1U);
}

TEST_F(ServiceHttpTests, RemovesTheUploadedFileWhenMetadataSavingFailsAndCanRetry)
{
    server::Service service;
    LocalHttpServer http(service);
    const auto metadata = utils::FileUtil(m_metadataFile).getContent();
    auto temporary = m_metadataFile;
    temporary += ".tmp";
    utils::FileUtil(temporary).setContent("occupied");
    const std::vector<std::pair<std::string, std::string>> headers{{"FileName", "YS50eHQ="}, {"StorageType", "uncompressed"}};
    EXPECT_EQ(http.post("/upload", headers, "data").m_code, HTTP_INTERNAL);
    EXPECT_FALSE(fs::exists(server::Config::GetInstance().getUncompressedFilesDirPath() / "a.txt"));
    EXPECT_TRUE(Access::data(service).getAll().empty());
    EXPECT_EQ(utils::FileUtil(m_metadataFile).getContent(), metadata);
    EXPECT_EQ(utils::FileUtil(temporary).getContent(), "occupied");
    fs::remove(temporary);
    EXPECT_EQ(http.post("/upload", headers, "retry").m_code, 201);
}

TEST_F(ServiceHttpTests, Returns500WhenTheStorageDirectoryCannotBeCreated)
{
    server::Service service;
    LocalHttpServer http(service);
    const auto directory = server::Config::GetInstance().getUncompressedFilesDirPath();
    utils::FileUtil(directory).setContent("occupied");
    EXPECT_EQ(http.post("/upload", {{"FileName", "YS50eHQ="}, {"StorageType", "uncompressed"}}, "data").m_code, HTTP_INTERNAL);
    EXPECT_EQ(utils::FileUtil(directory).getContent(), "occupied");
    EXPECT_TRUE(Access::data(service).getAll().empty());
}

TEST_F(ServiceRoutingTests, IgnoresQueryParametersWhenSelectingTheRootHandler)
{
    server::Service service;
    LocalHttpServer http(service);
    EXPECT_EQ(http.get("/?token=123&next=/download/a.txt").m_code, HTTP_OK);
}

TEST_F(ServiceRoutingTests, DecodesThePathBeforeRouting)
{
    server::Service service;
    LocalHttpServer http(service);
    EXPECT_EQ(http.get("/%2F").m_code, HTTP_NOTFOUND); // 解码后为 //，不是根路径 /。
    EXPECT_EQ(http.get("%2F").m_code, HTTP_OK);
}

TEST_F(ServiceRoutingTests, DoesNotDecodeThePathTwice)
{
    server::Service service;
    LocalHttpServer http(service);
    EXPECT_EQ(http.get("%252F").m_code, HTTP_NOTFOUND);
}

TEST_F(ServiceRoutingTests, Returns404ForUnknownAndCaseSensitivePaths)
{
    server::Service service;
    LocalHttpServer http(service);
    for (const char *path : {"/missing", "/UPLOAD", "/upload/", "/files", "/./", "/not-found?next=/download/a.txt"})
    {
        SCOPED_TRACE(path);
        const auto response = http.get(path);
        EXPECT_EQ(response.m_code, HTTP_NOTFOUND);
        EXPECT_TRUE(response.m_body.empty());
    }
}

TEST_F(ServiceRoutingTests, PropagatesInvalidPercentEncodingBeforeReplying)
{
    server::Service service;
    for (const char *path : {"/bad%", "/bad%Q1", "/bad%1Q"})
    {
        SCOPED_TRACE(path);
        auto request = MakeRequest(path);
        EXPECT_THROW(Access::dispatch(request.get(), service), std::invalid_argument);
        EXPECT_EQ(evbuffer_get_length(evhttp_request_get_output_buffer(request.get())), 0U);
    }
}

TEST_F(ServiceRunTests, ReportsTheConfiguredAddressWhenThePortIsAlreadyInUse)
{
    server::Service placeholder;
    LocalHttpServer occupiedPort(placeholder);
    m_config["server_ip"] = "127.0.0.1";
    m_config["server_port"] = occupiedPort.port();
    applyConfig();
    server::Service service;
    try
    {
        service.run();
        FAIL() << "Expected the occupied port to fail";
    }
    catch (const std::runtime_error &error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("evhttp_bind_socket() failed"), std::string::npos);
        EXPECT_NE(message.find("127.0.0.1:" + std::to_string(occupiedPort.port())), std::string::npos);
    }
    EXPECT_EQ(occupiedPort.get("/").m_code, HTTP_OK);
}

TEST_F(ServiceRunTests, RepeatedBindFailuresDoNotLeakEventLoopDescriptors)
{
    server::Service placeholder;
    LocalHttpServer occupiedPort(placeholder);
    m_config["server_ip"] = "127.0.0.1";
    m_config["server_port"] = occupiedPort.port();
    applyConfig();
    server::Service service;
    const auto descriptorCount = []
    {
        return std::distance(fs::directory_iterator("/proc/self/fd"), fs::directory_iterator{});
    };
    const auto before = descriptorCount();
    for (int attempt = 0; attempt < 10; ++attempt)
    {
        EXPECT_THROW(service.run(), std::runtime_error);
        EXPECT_EQ(descriptorCount(), before);
    }
}
