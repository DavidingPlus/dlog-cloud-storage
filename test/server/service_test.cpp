#include "service_test_util.h"

#include <base64.h>

#include <bundle.h>


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
    EXPECT_EQ(response.m_body, "<!DOCTYPE html>\n<html><body><div class='file-list'><h3>已上传文件</h3></div></body></html>\n");
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
}

TEST_F(ServiceHttpTests, ReplacesEveryOccurrenceOfTheFileListPlaceholder)
{
    writeTemplate("{{FILE_LIST}}|{{FILE_LIST}}");
    server::Service service;
    LocalHttpServer http(service);
    const auto body = http.get("/").m_body;
    EXPECT_EQ(Count(body, "<h3>已上传文件</h3>"), 2U);
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
    EXPECT_EQ(response.m_body.find("backendUrl"), std::string::npos);
    EXPECT_NE(response.m_body.find("fetch('/upload',"), std::string::npos);
}

TEST_F(ServiceHttpTests, UploadsAndDownloadsWithDifferentHostHeaders)
{
    writeTemplate(m_originalTemplate);
    server::Service service;
    LocalHttpServer http(service);
    // HTTP 连接仍发往本地测试端口；Host 模拟 IP、域名和代理对外提供的地址。
    const std::vector<std::pair<std::string, std::string>> origins{
        {"127.0.0.1:8081", "http://127.0.0.1:8081"},
        {"localhost:8081", "http://localhost:8081"},
        {"cloud.example:8081", "http://cloud.example:8081"},
        {"cloud.example", "https://cloud.example"}};
    const std::string content = std::string(4096, 'A') + std::string("\0\xffz", 3);
    for (const auto &origin : origins)
    {
        SCOPED_TRACE(origin.second);
        const std::vector<std::pair<std::string, std::string>> headers{{"Host", origin.first}, {"Origin", origin.second}};
        const auto page = http.get("/", headers);
        ASSERT_EQ(page.m_code, HTTP_OK);
        EXPECT_NE(page.m_body.find("fetch('/upload',"), std::string::npos);
        EXPECT_EQ(page.m_body.find("http://127.0.0.2:18081"), std::string::npos);
        for (const std::string type : {"uncompressed", "compressed"})
        {
            SCOPED_TRACE(type);
            const auto name = std::to_string(Access::data(service).getAll().size()) + u8"-中文 +.bin";
            auto uploadHeaders = headers;
            uploadHeaders.emplace_back("FileName", base64_encode(name));
            uploadHeaders.emplace_back("StorageType", type);
            const auto uploaded = http.post("/upload", uploadHeaders, content);
            ASSERT_EQ(uploaded.m_code, 201);
            const auto url = utils::JsonUtil::UnSerialize(uploaded.m_body)["url"].asString();
            EXPECT_EQ(url.find("/download/"), 0U);
            const auto pageWithFile = http.get("/", headers);
            ASSERT_EQ(pageWithFile.m_code, HTTP_OK);
            EXPECT_NE(pageWithFile.m_body.find(url), std::string::npos);
            const auto downloaded = http.get(url, headers);
            ASSERT_EQ(downloaded.m_code, HTTP_OK);
            EXPECT_EQ(downloaded.m_body, content);
        }
    }
}

TEST_F(ServiceHttpTests, Returns500ForMissingTemplateAndRecovers)
{
    server::Service service;
    LocalHttpServer http(service);
    fs::remove(m_templateFile);
    const auto failed = http.get("/");
    EXPECT_EQ(failed.m_code, HTTP_INTERNAL);
    EXPECT_TRUE(failed.m_body.empty());
    EXPECT_EQ(http.get("/missing").m_code, HTTP_NOTFOUND);

    writeTemplate("<p>recovered</p>");
    const auto recovered = http.get("/");
    EXPECT_EQ(recovered.m_code, HTTP_OK);
    EXPECT_EQ(recovered.m_body, "<p>recovered</p>");
}

TEST_F(ServiceHttpTests, Returns500ForDirectoryAtTheTemplatePathAndRecovers)
{
    server::Service service;
    LocalHttpServer http(service);
    fs::remove(m_templateFile);
    fs::create_directory(m_templateFile);
    EXPECT_EQ(http.get("/").m_code, HTTP_INTERNAL);
    EXPECT_EQ(http.get("/missing").m_code, HTTP_NOTFOUND);

    fs::remove(m_templateFile);
    writeTemplate("<p>recovered</p>");
    const auto recovered = http.get("/");
    EXPECT_EQ(recovered.m_code, HTTP_OK);
    EXPECT_EQ(recovered.m_body, "<p>recovered</p>");
}

TEST_F(ServiceHttpTests, Returns500ForUnreadableTemplateAndRecovers)
{
    server::Service service;
    LocalHttpServer http(service);
    const auto permissions = fs::status(m_templateFile).permissions();
    fs::permissions(m_templateFile, fs::perms::none);
    std::ifstream probe(m_templateFile);
    if (probe.is_open()) GTEST_SKIP() << "The current user can read files without read permission";
    EXPECT_EQ(http.get("/").m_code, HTTP_INTERNAL);
    EXPECT_EQ(http.get("/missing").m_code, HTTP_NOTFOUND);

    fs::permissions(m_templateFile, permissions);
    EXPECT_EQ(http.get("/").m_code, HTTP_OK);
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
        const auto info = Access::data(service).getFileMetaDataByRequestPath(url);
        EXPECT_EQ(info.m_absoluteFilePath, path);
        EXPECT_EQ(info.m_fileSize, file.second.size());
        EXPECT_EQ(info.m_mtime, fs::last_write_time(path));
        server::DataManager restored;
        EXPECT_EQ(restored.getFileMetaDataByRequestPath(url).m_absoluteFilePath, path);
    }
    EXPECT_EQ(Count(http.get("/").m_body, "class='file-item'"), files.size());
}

TEST_F(ServiceHttpTests, RejectsInvalidUploadParametersBeforeCreatingFiles)
{
    server::Service service;
    LocalHttpServer http(service);
    EXPECT_EQ(http.get("/upload").m_code, HTTP_BADMETHOD);
    for (const char *type : {"unknown", "", "Compressed", "UNCOMPRESSED"})
        EXPECT_EQ(http.post("/upload", {{"FileName", "YS50eHQ="}, {"StorageType", type}}, "data").m_code, HTTP_BADREQUEST);
    for (const char *name : {"", "!m9v", "Z"})
        EXPECT_EQ(http.post("/upload", {{"FileName", name}, {"StorageType", "uncompressed"}}, "data").m_code, HTTP_BADREQUEST);
    EXPECT_TRUE(Access::data(service).getAll().empty());
    EXPECT_FALSE(fs::exists(server::Config::GetInstance().getUncompressedFilesDirPath()));
}

TEST_F(ServiceHttpTests, RejectsFileNamesThatEscapeTheStorageDirectory)
{
    server::Service service;
    LocalHttpServer http(service);
    const auto absolutePath = m_directory / "absolute-upload.txt";
    const auto escapedPath = m_directory / "escaped-upload.txt";
    const auto metadata = utils::FileUtil(m_metadataFile).getContent();
    for (const std::string type : {"uncompressed", "compressed"})
    {
        SCOPED_TRACE(type);
        for (const auto &name : std::vector<std::string>{"../escaped-upload.txt", absolutePath.string()})
        {
            SCOPED_TRACE(name);
            const auto response = http.post("/upload", {{"FileName", base64_encode(name)}, {"StorageType", type}}, "data");
            EXPECT_EQ(response.m_code, HTTP_BADREQUEST);
            EXPECT_TRUE(Access::data(service).getAll().empty());
            EXPECT_EQ(utils::FileUtil(m_metadataFile).getContent(), metadata);
            EXPECT_FALSE(fs::exists(absolutePath));
            EXPECT_FALSE(fs::exists(escapedPath));
            EXPECT_FALSE(fs::exists(server::Config::GetInstance().getUncompressedFilesDirPath()));
            EXPECT_FALSE(fs::exists(server::Config::GetInstance().getCompressedFilesDirPath()));
        }
    }
}

TEST_F(ServiceHttpTests, RejectsInvalidFileNamesBeforeCreatingFiles)
{
    server::Service service;
    LocalHttpServer http(service);
    const auto metadata = utils::FileUtil(m_metadataFile).getContent();
    const std::vector<std::string> names{
        "", ".", "..", "./a.txt", "a/b.txt", "a/../b.txt", "a/", std::string(1, '\0'), std::string("a\0.txt", 6),
        std::string(256, 'a'), std::string(252, 'a') + u8"😀", std::string(254, 'a') + u8"中",
        "\x80", "\xbf", "\xc0\xaf", "\xc1\xbf", "\xc2", "\xc2 ", "\xe0\x80\xaf", "\xe2\x82", "\xe2(\xa1",
        "\xed\xa0\x80", "\xed\xbf\xbf", "\xf0\x80\x80\xaf", "\xf0\x9f\x98", "\xf0(\x8c\xbc", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xff"};

    for (const std::string type : {"uncompressed", "compressed"})
    {
        SCOPED_TRACE(type);
        for (const auto &name : names)
        {
            const auto encoded = base64_encode(name);
            SCOPED_TRACE(encoded);
            EXPECT_EQ(http.post("/upload", {{"FileName", encoded}, {"StorageType", type}}, "data").m_code, HTTP_BADREQUEST);
        }
    }
    EXPECT_TRUE(Access::data(service).getAll().empty());
    EXPECT_EQ(utils::FileUtil(m_metadataFile).getContent(), metadata);
    EXPECT_FALSE(fs::exists(server::Config::GetInstance().getUncompressedFilesDirPath()));
    EXPECT_FALSE(fs::exists(server::Config::GetInstance().getCompressedFilesDirPath()));
}

TEST_F(ServiceHttpTests, UploadsLinuxFileNamesWithBackslashesAndControlCharacters)
{
    std::vector<std::string> names{"a\\b.txt", "\\a.txt", "C:\\a.txt", "\\\\server\\share\\a.txt", "..\\outside.txt", "a\x7f.txt"};
    // Linux 允许这些普通文件名字节；UTF-8 编码有效且不含 NUL 即可。
    for (unsigned int ch = 1; ch < 32; ++ch) names.push_back("a" + std::string(1, static_cast<char>(ch)) + ".txt");
    for (unsigned int ch = 0x80; ch <= 0x9f; ++ch) names.push_back("a\xc2" + std::string(1, static_cast<char>(ch)) + ".txt");

    const std::string content = std::string(4096, 'A') + std::string("\0\xffz", 3);
    std::vector<std::string> urls;
    {
        server::Service service;
        LocalHttpServer http(service);
        for (const std::string type : {"uncompressed", "compressed"})
        {
            SCOPED_TRACE(type);
            for (const auto &baseName : names)
            {
                const auto name = baseName + "-" + type;
                SCOPED_TRACE(base64_encode(name));
                const auto uploaded = http.post("/upload", {{"FileName", base64_encode(name)}, {"StorageType", type}}, content);
                ASSERT_EQ(uploaded.m_code, 201);
                const auto url = utils::JsonUtil::UnSerialize(uploaded.m_body)["url"].asString();
                EXPECT_EQ(url, utils::urlEncode("/download/" + name));
                urls.push_back(url);
                const auto info = Access::data(service).getFileMetaDataByRequestPath("/download/" + name);
                EXPECT_EQ(info.m_absoluteFilePath.filename().string(), name);
                const auto downloaded = http.get(url);
                ASSERT_EQ(downloaded.m_code, HTTP_OK);
                EXPECT_EQ(downloaded.m_body, content);
                EXPECT_EQ(downloaded.m_contentDisposition, "attachment; filename*=UTF-8''" + utils::fileNameEncode(name));
            }
        }
    }

    // 控制字符和反斜杠经过 JSON 持久化及重建服务后，仍须按原始名称找到文件。
    server::Service restored;
    LocalHttpServer http(restored);
    EXPECT_EQ(Access::data(restored).getAll().size(), urls.size());
    for (const auto &url : urls)
    {
        SCOPED_TRACE(url);
        const auto downloaded = http.get(url);
        ASSERT_EQ(downloaded.m_code, HTTP_OK);
        EXPECT_EQ(downloaded.m_body, content);
    }
}

TEST_F(ServiceHttpTests, InvalidFileNamesPreserveExistingFilesAndMetadata)
{
    server::Service service;
    LocalHttpServer http(service);
    const auto outsidePath = m_directory / "outside.txt";
    utils::FileUtil(outsidePath).setContent("outside");
    for (const std::string type : {"uncompressed", "compressed"})
    {
        SCOPED_TRACE(type);
        const auto name = type + ".txt";
        ASSERT_EQ(http.post("/upload", {{"FileName", base64_encode(name)}, {"StorageType", type}}, "original").m_code, 201);
        const auto metadata = utils::FileUtil(m_metadataFile).getContent();
        const auto count = Access::data(service).getAll().size();
        for (const auto &invalidName : std::vector<std::string>{"./" + name, "../outside.txt", outsidePath.string(), name + std::string("\0suffix", 7)})
        {
            SCOPED_TRACE(base64_encode(invalidName));
            EXPECT_EQ(http.post("/upload", {{"FileName", base64_encode(invalidName)}, {"StorageType", type}}, "replacement").m_code, HTTP_BADREQUEST);
            EXPECT_EQ(Access::data(service).getAll().size(), count);
            EXPECT_EQ(utils::FileUtil(m_metadataFile).getContent(), metadata);
            EXPECT_EQ(utils::FileUtil(outsidePath).getContent(), "outside");
            const auto downloaded = http.get("/download/" + name);
            EXPECT_EQ(downloaded.m_code, HTTP_OK);
            EXPECT_EQ(downloaded.m_body, "original");
        }
    }
}

TEST_F(ServiceHttpTests, UploadsValidUtf8FileNamesAndNamesAtTheByteLimit)
{
    server::Service service;
    LocalHttpServer http(service);
    const std::string content = std::string(4096, 'A') + std::string("\0\xffz", 3);
    for (const std::string type : {"uncompressed", "compressed"})
    {
        SCOPED_TRACE(type);
        std::string chineseLimit;
        for (int i = 0; i < 84; ++i) chineseLimit += u8"中";
        chineseLimit += type.substr(0, 1) + ".x";
        const std::vector<std::string> names{
            "." + type, type + " . +%2F?#&\"'<>:|*.txt", u8"中文 😀 " + type + ".txt",
            std::string(254, 'a') + type.substr(0, 1), chineseLimit,
            type + "\xc2\xa0\xdf\xbf\xe0\xa0\x80\xed\x9f\xbf\xee\x80\x80\xef\xbf\xbf\xf0\x90\x80\x80\xf4\x8f\xbf\xbf.bin"};
        for (const auto &name : names)
        {
            SCOPED_TRACE(base64_encode(name));
            const auto uploaded = http.post("/upload", {{"FileName", base64_encode(name)}, {"StorageType", type}}, content);
            ASSERT_EQ(uploaded.m_code, 201);
            const auto url = utils::JsonUtil::UnSerialize(uploaded.m_body)["url"].asString();
            EXPECT_EQ(url, utils::urlEncode("/download/" + name));
            const auto downloaded = http.get(url);
            ASSERT_EQ(downloaded.m_code, HTTP_OK);
            EXPECT_EQ(downloaded.m_body, content);
            EXPECT_EQ(downloaded.m_contentDisposition, "attachment; filename*=UTF-8''" + utils::fileNameEncode(name));
        }
    }
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
        EXPECT_EQ(response.m_contentType, "application/json;charset=utf-8");
        const auto path = server::Config::GetInstance().getCompressedFilesDirPath() / file.first;
        const auto stored = utils::FileUtil(path).getContent();
        EXPECT_EQ(utils::FileUtil::UnCompress(stored), file.second);
        if (file.second.empty()) EXPECT_TRUE(stored.empty());
        const auto url = "/download/" + file.first;
        const auto info = Access::data(service).getFileMetaDataByRequestPath(url);
        EXPECT_EQ(info.m_absoluteFilePath, path);
        EXPECT_EQ(info.m_fileSize, stored.size());
        EXPECT_EQ(info.m_mtime, fs::last_write_time(path));
        server::DataManager restored;
        EXPECT_EQ(restored.getFileMetaDataByRequestPath(url).m_fileSize, stored.size());
        EXPECT_EQ(utils::JsonUtil::UnSerialize(response.m_body)["url"].asString(), utils::urlEncode(url));
        // 另一种策略使用同一下载 URL，也必须拒绝，保留原压缩文件。
        EXPECT_EQ(http.post("/upload", {{"FileName", base64_encode(file.first)}, {"StorageType", "uncompressed"}}, "replacement").m_code, 409);
        EXPECT_EQ(utils::FileUtil(path).getContent(), stored);
    }
    const auto path = server::Config::GetInstance().getCompressedFilesDirPath() / files.front().first;
    EXPECT_LT(utils::FileUtil(path).fileSize(), files.front().second.size());
}

TEST_F(ServiceHttpTests, CompressedUploadsUseTheConfiguredBundleFormat)
{
    // RAW 保留原始字节；使用可压缩内容，确认 Upload 没有忽略配置而固定使用默认压缩算法。
    m_config["bundle_format"] = bundle::RAW;
    applyConfig();
    server::Service service;
    LocalHttpServer http(service);
    const std::string content(32768, 'A');
    const std::string name = "configured.txt";
    ASSERT_EQ(http.post("/upload", {{"FileName", base64_encode(name)}, {"StorageType", "compressed"}}, content).m_code, 201);

    const auto path = server::Config::GetInstance().getCompressedFilesDirPath() / name;
    EXPECT_EQ(utils::FileUtil(path).getContent(), content);
    const auto info = Access::data(service).getFileMetaDataByRequestPath("/download/" + name);
    EXPECT_EQ(info.m_absoluteFilePath, path);
    EXPECT_EQ(info.m_fileSize, content.size());
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

TEST_F(ServiceHttpTests, RejectsUploadsAtExistingAndDanglingSymlinks)
{
    server::Service service;
    LocalHttpServer http(service);
    const auto directory = server::Config::GetInstance().getUncompressedFilesDirPath();
    fs::create_directories(directory);
    const auto target = m_directory / "original.txt";
    const auto missingTarget = m_directory / "missing.txt";
    utils::FileUtil(target).setContent("keep");
    fs::create_symlink(target, directory / "linked.txt");
    fs::create_symlink(missingTarget, directory / "dangling.txt");
    const auto metadata = utils::FileUtil(m_metadataFile).getContent();

    for (const std::string name : {"linked.txt", "dangling.txt"})
    {
        SCOPED_TRACE(name);
        EXPECT_EQ(http.post("/upload", {{"FileName", base64_encode(name)}, {"StorageType", "uncompressed"}}, "replacement").m_code, 409);
        EXPECT_TRUE(fs::is_symlink(directory / name));
    }
    EXPECT_EQ(utils::FileUtil(target).getContent(), "keep");
    EXPECT_FALSE(fs::exists(missingTarget));
    EXPECT_TRUE(Access::data(service).getAll().empty());
    EXPECT_EQ(utils::FileUtil(m_metadataFile).getContent(), metadata);
}

TEST_F(ServiceHttpTests, RemovesTheUploadedFileWhenMetadataSavingFailsAndCanRetry)
{
    server::Service service;
    LocalHttpServer http(service);
    const std::string existingName = "existing.txt";
    ASSERT_EQ(http.post("/upload", {{"FileName", base64_encode(existingName)}, {"StorageType", "uncompressed"}}, "keep").m_code, 201);
    const auto existingPath = server::Config::GetInstance().getUncompressedFilesDirPath() / existingName;
    auto temporary = m_metadataFile;
    temporary += ".tmp";

    // 两种策略均验证“数据文件已写入，但元数据无法保存”时的清理、旧记录保护和重试。
    for (const std::string storageType : {"uncompressed", "compressed"})
    {
        SCOPED_TRACE(storageType);
        const auto metadata = utils::FileUtil(m_metadataFile).getContent();
        const auto originalCount = Access::data(service).getAll().size();
        const auto name = storageType + ".txt";
        const auto url = "/download/" + name;
        const auto directory = "compressed" == storageType ? server::Config::GetInstance().getCompressedFilesDirPath() : server::Config::GetInstance().getUncompressedFilesDirPath();
        const auto path = directory / name;
        const std::vector<std::pair<std::string, std::string>> headers{{"FileName", base64_encode(name)}, {"StorageType", storageType}};
        utils::FileUtil(temporary).setContent("occupied");

        EXPECT_EQ(http.post("/upload", headers, std::string(4096, 'A')).m_code, HTTP_INTERNAL);
        EXPECT_FALSE(fs::exists(path));
        EXPECT_EQ(Access::data(service).getAll().size(), originalCount);
        EXPECT_THROW(Access::data(service).getFileMetaDataByRequestPath(url), std::out_of_range);
        EXPECT_EQ(utils::FileUtil(m_metadataFile).getContent(), metadata);
        EXPECT_EQ(utils::FileUtil(existingPath).getContent(), "keep");
        EXPECT_EQ(utils::FileUtil(temporary).getContent(), "occupied");
        server::DataManager restored;
        EXPECT_EQ(restored.getAll().size(), originalCount);
        EXPECT_THROW(restored.getFileMetaDataByRequestPath(url), std::out_of_range);

        fs::remove(temporary);
        ASSERT_EQ(http.post("/upload", headers, "retry").m_code, 201);
        auto stored = utils::FileUtil(path).getContent();
        if ("compressed" == storageType) stored = utils::FileUtil::UnCompress(stored);
        EXPECT_EQ(stored, "retry");
    }
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

TEST_F(ServiceHttpTests, Returns500WhenTheUploadedFileCannotBeOpenedAndCanRetry)
{
    server::Service service;
    LocalHttpServer http(service);
    const auto directory = server::Config::GetInstance().getUncompressedFilesDirPath();
    fs::create_directories(directory);
    const auto metadata = utils::FileUtil(m_metadataFile).getContent();
    fs::permissions(directory, fs::perms::owner_read | fs::perms::owner_exec);
    std::ofstream probe(directory / "probe.txt");
    if (probe.is_open())
    {
        probe.close();
        fs::permissions(directory, fs::perms::owner_all);
        GTEST_SKIP() << "The current user can write to a directory without write permission";
    }

    // 目录已经存在，createDirectory() 可以成功，随后 setContent() 打开新文件时失败。
    const std::vector<std::pair<std::string, std::string>> headers{{"FileName", "YS50eHQ="}, {"StorageType", "uncompressed"}};
    const auto response = http.post("/upload", headers, "data");
    fs::permissions(directory, fs::perms::owner_all);
    EXPECT_EQ(response.m_code, HTTP_INTERNAL);
    EXPECT_FALSE(fs::exists(directory / "a.txt"));
    EXPECT_TRUE(Access::data(service).getAll().empty());
    EXPECT_EQ(utils::FileUtil(m_metadataFile).getContent(), metadata);

    ASSERT_EQ(http.post("/upload", headers, "retry").m_code, 201);
    EXPECT_EQ(utils::FileUtil(directory / "a.txt").getContent(), "retry");
}

TEST_F(ServiceHttpTests, DownloadsUncompressedUploadsAsOriginalBytes)
{
    server::Service service;
    LocalHttpServer http(service);
    // 已压缩的内容也可以按普通存储上传，下载必须原样返回，不能根据内容猜测是否解压。
    const std::vector<std::pair<std::string, std::string>> files{
        {"hello.txt", "hello download\n"},
        {u8"中文 空格+%?#.bin", std::string("a\0\xffz", 4)},
        {"empty.txt", ""},
        {"literal%2F.txt", "percent filename"},
        {"already-packed.bin", utils::FileUtil::Compress(std::string(4096, 'A'), bundle::LZIP)}};
    for (const auto &file : files)
    {
        SCOPED_TRACE(file.first);
        const auto uploaded = http.post("/upload", {{"FileName", base64_encode(file.first)}, {"StorageType", "uncompressed"}}, file.second);
        ASSERT_EQ(uploaded.m_code, 201);
        const auto url = utils::JsonUtil::UnSerialize(uploaded.m_body)["url"].asString();
        const auto info = Access::data(service).getFileMetaDataByRequestPath("/download/" + file.first);
        const auto metadata = utils::FileUtil(m_metadataFile).getContent();
        const auto response = http.get(url);

        ASSERT_EQ(response.m_code, HTTP_OK);
        EXPECT_EQ(response.m_body, file.second);
        EXPECT_EQ(response.m_contentType, "application/octet-stream");
        EXPECT_EQ(response.m_contentLength, std::to_string(file.second.size()));
        EXPECT_EQ(response.m_contentDisposition, "attachment; filename*=UTF-8''" + utils::fileNameEncode(file.first));
        EXPECT_EQ(response.m_etag, Access::etag(info));
        EXPECT_EQ(utils::FileUtil(info.m_absoluteFilePath).getContent(), file.second);
        EXPECT_EQ(utils::FileUtil(m_metadataFile).getContent(), metadata);
    }
    EXPECT_EQ(Access::data(service).getAll().size(), files.size());
}

TEST_F(ServiceHttpTests, DownloadsCompressedUploadsWithoutTemporaryFiles)
{
    server::Service service;
    LocalHttpServer http(service);
    const auto ordinaryDirectory = server::Config::GetInstance().getUncompressedFilesDirPath();
    const std::vector<std::pair<std::string, std::string>> files{
        {u8"压缩 +.txt", std::string(32768, 'A')},
        {"binary.bin", std::string("a\0\xffz", 4)},
        {"empty.txt", ""}};
    for (const auto &file : files)
    {
        SCOPED_TRACE(file.first);
        const auto uploaded = http.post("/upload", {{"FileName", base64_encode(file.first)}, {"StorageType", "compressed"}}, file.second);
        ASSERT_EQ(uploaded.m_code, 201);
        const auto url = utils::JsonUtil::UnSerialize(uploaded.m_body)["url"].asString();
        const auto info = Access::data(service).getFileMetaDataByRequestPath("/download/" + file.first);
        const auto stored = utils::FileUtil(info.m_absoluteFilePath).getContent();
        const auto metadata = utils::FileUtil(m_metadataFile).getContent();
        const auto response = http.get(url);

        ASSERT_EQ(response.m_code, HTTP_OK);
        EXPECT_EQ(response.m_body, file.second);
        EXPECT_EQ(response.m_contentLength, std::to_string(file.second.size()));
        EXPECT_EQ(response.m_contentType, "application/octet-stream");
        EXPECT_EQ(response.m_etag, Access::etag(info));
        EXPECT_EQ(utils::FileUtil(info.m_absoluteFilePath).getContent(), stored);
        EXPECT_EQ(utils::FileUtil(m_metadataFile).getContent(), metadata);
        EXPECT_FALSE(fs::exists(ordinaryDirectory));
    }

    // 普通目录中即使已有同名文件，也不能被压缩下载覆盖或删除。
    fs::create_directories(ordinaryDirectory);
    const auto existing = ordinaryDirectory / files.front().first;
    utils::FileUtil(existing).setContent("keep");
    const auto response = http.get(utils::urlEncode("/download/" + files.front().first));
    EXPECT_EQ(response.m_code, HTTP_OK);
    EXPECT_EQ(response.m_body, files.front().second);
    EXPECT_EQ(utils::FileUtil(existing).getContent(), "keep");
}

TEST_F(ServiceHttpTests, DownloadsWithTrailingSeparatorsInConfiguredStorageDirectories)
{
    const std::string content = std::string(32768, 'A') + std::string("\0\xffz", 3);
    for (bool useRelativePaths : {false, true})
    {
        SCOPED_TRACE(useRelativePaths ? "relative directories" : "absolute directories");
        const auto directory = m_directory / (useRelativePaths ? "relative" : "absolute");
        for (const char *type : {"uncompressed", "compressed"})
        {
            const auto path = directory / type;
            const auto configuredPath = useRelativePaths ? path.lexically_relative(utils::executableDirectory()) : path;
            m_config[std::string(type) + "_files_dir"] = configuredPath.string() + "/";
        }
        applyConfig();
        writeMetadata({});

        std::vector<std::string> urls;
        {
            server::Service service;
            LocalHttpServer http(service);
            for (const std::string type : {"uncompressed", "compressed"})
            {
                SCOPED_TRACE(type);
                const auto uploaded = http.post("/upload", {{"FileName", base64_encode(type + ".bin")}, {"StorageType", type}}, content);
                ASSERT_EQ(uploaded.m_code, 201);
                urls.push_back(utils::JsonUtil::UnSerialize(uploaded.m_body)["url"].asString());
                const auto response = http.get(urls.back());
                ASSERT_EQ(response.m_code, HTTP_OK);
                EXPECT_EQ(response.m_body, content);
                EXPECT_EQ(response.m_contentLength, std::to_string(content.size()));
            }
        }

        // 重建服务后，带末尾分隔符的配置也必须能识别持久化记录中的存储位置。
        server::Service restored;
        LocalHttpServer http(restored);
        for (const auto &url : urls)
        {
            SCOPED_TRACE(url);
            const auto response = http.get(url);
            ASSERT_EQ(response.m_code, HTTP_OK);
            EXPECT_EQ(response.m_body, content);
        }
    }
}

TEST_F(ServiceHttpTests, DownloadsUsingConfiguredPrefixAndDecodesTheOriginalPathOnce)
{
    m_config["download_prefix"] = "/files/";
    applyConfig();
    server::Service service;
    LocalHttpServer http(service);
    const std::string name = u8"中文 +%2F.txt";
    const auto uploaded = http.post("/upload", {{"FileName", base64_encode(name)}, {"StorageType", "uncompressed"}}, "original");
    ASSERT_EQ(uploaded.m_code, 201);
    const auto url = utils::JsonUtil::UnSerialize(uploaded.m_body)["url"].asString();
    const auto response = http.get(url + "?token=123&next=/download/missing.txt");
    EXPECT_EQ(response.m_code, HTTP_OK);
    EXPECT_EQ(response.m_body, "original");
    EXPECT_EQ(response.m_contentDisposition, "attachment; filename*=UTF-8''" + utils::fileNameEncode(name));
    EXPECT_EQ(http.get(utils::urlEncode("/download/" + name)).m_code, HTTP_NOTFOUND);
}

TEST_F(ServiceHttpTests, DownloadsUsingTheFilePathRecordedInMetadata)
{
    server::Service service;
    LocalHttpServer http(service);
    const auto directory = server::Config::GetInstance().getUncompressedFilesDirPath();
    fs::create_directories(directory);
    const auto path = directory / "stored.bin";
    const std::string content("a\0\xffz", 4);
    utils::FileUtil(path).setContent(content);
    auto info = server::FileMetadata::FromFile(path);
    info.m_url = "/download/alias.bin";
    Access::data(service).insert(info);

    const auto response = http.get(info.m_url);
    EXPECT_EQ(response.m_code, HTTP_OK);
    EXPECT_EQ(response.m_body, content);
    EXPECT_EQ(response.m_contentDisposition, "attachment; filename*=UTF-8''stored.bin");
    EXPECT_EQ(http.get("/download/stored.bin").m_code, HTTP_NOTFOUND);
}

TEST_F(ServiceHttpTests, DownloadsAfterRecreatingTheServiceFromPersistedMetadata)
{
    const std::vector<std::pair<std::string, std::string>> files{
        {"persisted.bin", std::string("a\0\xffz", 4)},
        {u8"持久化 压缩.bin", std::string(32768, 'A') + std::string("\0\xff", 2)}};
    std::vector<std::string> urls;
    std::vector<server::FileMetadata> records;
    {
        server::Service service;
        LocalHttpServer http(service);
        for (size_t i = 0; i < files.size(); ++i)
        {
            SCOPED_TRACE(files[i].first);
            const auto type = i == 0 ? "uncompressed" : "compressed";
            const auto uploaded = http.post("/upload", {{"FileName", base64_encode(files[i].first)}, {"StorageType", type}}, files[i].second);
            ASSERT_EQ(uploaded.m_code, 201);
            urls.push_back(utils::JsonUtil::UnSerialize(uploaded.m_body)["url"].asString());
            records.push_back(Access::data(service).getFileMetaDataByRequestPath("/download/" + files[i].first));
        }
    }

    // 原 Service 及其内存表已销毁，新实例必须从磁盘元数据恢复普通和压缩文件的下载信息。
    const auto metadata = utils::FileUtil(m_metadataFile).getContent();
    server::Service restored;
    LocalHttpServer http(restored);
    ASSERT_EQ(Access::data(restored).getAll().size(), files.size());
    for (size_t i = 0; i < files.size(); ++i)
    {
        SCOPED_TRACE(files[i].first);
        const auto response = http.get(urls[i]);
        ASSERT_EQ(response.m_code, HTTP_OK);
        EXPECT_EQ(response.m_body, files[i].second);
        EXPECT_EQ(response.m_contentLength, std::to_string(files[i].second.size()));
        EXPECT_EQ(response.m_contentDisposition, "attachment; filename*=UTF-8''" + utils::fileNameEncode(files[i].first));
        EXPECT_EQ(response.m_etag, Access::etag(records[i]));
        EXPECT_EQ(Access::data(restored).getFileMetaDataByRequestPath(records[i].m_url).m_absoluteFilePath, records[i].m_absoluteFilePath);
    }
    EXPECT_EQ(utils::FileUtil(m_metadataFile).getContent(), metadata);
}

TEST_F(ServiceHttpTests, DownloadsReturn404ForMissingRecordsAndMissingDataFiles)
{
    server::Service service;
    LocalHttpServer http(service);
    EXPECT_EQ(http.get("/download/missing.txt").m_code, HTTP_NOTFOUND);
    for (const char *type : {"uncompressed", "compressed"})
    {
        SCOPED_TRACE(type);
        const auto name = std::string(type) + ".txt";
        ASSERT_EQ(http.post("/upload", {{"FileName", base64_encode(name)}, {"StorageType", type}}, "data").m_code, 201);
        const auto info = Access::data(service).getFileMetaDataByRequestPath("/download/" + name);
        const auto metadata = utils::FileUtil(m_metadataFile).getContent();
        fs::remove(info.m_absoluteFilePath);

        EXPECT_EQ(http.get(info.m_url).m_code, HTTP_NOTFOUND);
        EXPECT_NO_THROW(Access::data(service).getFileMetaDataByRequestPath(info.m_url));
        EXPECT_EQ(utils::FileUtil(m_metadataFile).getContent(), metadata);
    }
}

TEST_F(ServiceHttpTests, DownloadsReturn500WhenTheStoredPathIsADirectory)
{
    server::Service service;
    LocalHttpServer http(service);
    const auto info = makeInfo("directory.txt");
    fs::create_directories(info.m_absoluteFilePath);
    Access::data(service).insert(info);
    const auto metadata = utils::FileUtil(m_metadataFile).getContent();

    EXPECT_EQ(http.get(info.m_url).m_code, HTTP_INTERNAL);
    EXPECT_TRUE(fs::is_directory(info.m_absoluteFilePath));
    EXPECT_EQ(utils::FileUtil(m_metadataFile).getContent(), metadata);
}

TEST_F(ServiceHttpTests, DownloadsReturn500ForUnreadableFilesAndCanRetry)
{
    server::Service service;
    LocalHttpServer http(service);
    const std::string content(32768, 'A');
    for (const char *type : {"uncompressed", "compressed"})
    {
        SCOPED_TRACE(type);
        const auto name = std::string(type) + ".bin";
        ASSERT_EQ(http.post("/upload", {{"FileName", base64_encode(name)}, {"StorageType", type}}, content).m_code, 201);
        const auto info = Access::data(service).getFileMetaDataByRequestPath("/download/" + name);
        const auto metadata = utils::FileUtil(m_metadataFile).getContent();
        const auto permissions = fs::status(info.m_absoluteFilePath).permissions();
        fs::permissions(info.m_absoluteFilePath, fs::perms::none);
        std::ifstream probe(info.m_absoluteFilePath, std::ios::binary);
        if (probe.is_open())
        {
            probe.close();
            fs::permissions(info.m_absoluteFilePath, permissions);
            GTEST_SKIP() << "The current user can read files without read permission";
        }

        // 文件仍然存在，但读取失败应返回 500；失败不能删除文件或元数据。
        const auto failed = http.get(info.m_url);
        fs::permissions(info.m_absoluteFilePath, permissions);
        EXPECT_EQ(failed.m_code, HTTP_INTERNAL);
        EXPECT_EQ(utils::FileUtil(m_metadataFile).getContent(), metadata);
        EXPECT_NO_THROW(Access::data(service).getFileMetaDataByRequestPath(info.m_url));

        const auto retried = http.get(info.m_url);
        EXPECT_EQ(retried.m_code, HTTP_OK);
        EXPECT_EQ(retried.m_body, content);
    }
}

TEST_F(ServiceHttpTests, RejectsNonGetDownloadRequests)
{
    server::Service service;
    LocalHttpServer http(service);
    EXPECT_EQ(http.post("/download/missing.txt", {}, "data").m_code, HTTP_BADMETHOD);
    EXPECT_TRUE(Access::data(service).getAll().empty());
}

TEST_F(ServiceHttpTests, DownloadsCompleteFilesWhileRangeAndCacheConditionsAreDeferred)
{
    server::Service service;
    LocalHttpServer http(service);
    ASSERT_EQ(http.post("/upload", {{"FileName", "YS50eHQ="}, {"StorageType", "uncompressed"}}, "0123456789").m_code, 201);
    const auto first = http.get("/download/a.txt");
    ASSERT_EQ(first.m_code, HTTP_OK);
    ASSERT_FALSE(first.m_etag.empty());

    // Range 尚未实现时仍返回完整 200，不能像参考实现一样仅凭 If-Range 返回假的 206。
    const auto range = http.get("/download/a.txt", {{"Range", "bytes=2-4"}, {"If-Range", first.m_etag}});
    EXPECT_EQ(range.m_code, HTTP_OK);
    EXPECT_EQ(range.m_body, "0123456789");
    EXPECT_EQ(range.m_contentLength, "10");
    EXPECT_TRUE(range.m_acceptRanges.empty());
    // 当前不处理缓存条件请求；即使 If-None-Match 匹配，也返回 200 和完整正文。
    const auto cached = http.get("/download/a.txt", {{"If-None-Match", first.m_etag}});
    EXPECT_EQ(cached.m_code, HTTP_OK);
    EXPECT_EQ(cached.m_body, "0123456789");
}

TEST_F(ServiceRoutingTests, IgnoresQueryParametersWhenSelectingTheRootHandler)
{
    server::Service service;
    LocalHttpServer http(service);
    EXPECT_EQ(http.get("/?token=123&next=/download/a.txt").m_code, HTTP_OK);
}

TEST_F(ServiceRoutingTests, MatchesDownloadPrefixesOnlyAtTheStartOfThePath)
{
    for (const std::string prefix : {"/download/", "/files/"})
    {
        SCOPED_TRACE(prefix);
        m_config["download_prefix"] = prefix;
        applyConfig();
        server::Service service;
        LocalHttpServer http(service);

        // 正确前缀仍进入下载处理；前缀仅出现在路径中间时，GET 和 POST 都应作为未知路由返回 404。
        EXPECT_EQ(http.post(prefix + "missing.txt", {}, "data").m_code, HTTP_BADMETHOD);
        const auto unknownPath = "/other" + prefix + "missing.txt";
        EXPECT_EQ(http.get(unknownPath).m_code, HTTP_NOTFOUND);
        EXPECT_EQ(http.post(unknownPath, {}, "data").m_code, HTTP_NOTFOUND);
    }
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

TEST_F(ServiceRoutingTests, Returns400ForInvalidPercentEncodingAndContinuesServingRequests)
{
    server::Service service;
    LocalHttpServer http(service);
    for (const char *path : {"/bad%", "/bad%Q1", "/bad%1Q", "/download/bad%", "/upload%Q1"})
    {
        SCOPED_TRACE(path);
        const auto response = http.get(path);
        EXPECT_EQ(response.m_code, HTTP_BADREQUEST);
        EXPECT_TRUE(response.m_body.empty());
        EXPECT_EQ(http.get("/").m_code, HTTP_OK);
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
