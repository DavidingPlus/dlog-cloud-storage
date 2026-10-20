#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "server/config.h"
#include "server/datamanager.h"
#include "utils/common.h"
#include "utils/fileutil.h"
#include "utils/jsonutil.h"


namespace fs = std::filesystem;

namespace
{
    class DataManagerTests : public ::testing::Test
    {

    protected:

        void SetUp() override
        {
            m_originalWorkingDirectory = fs::current_path();
            m_configFile = utils::executableDirectory() / "config" / "server.conf";
            m_originalConfig = utils::FileUtil(m_configFile).getContent();

            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (int attempt = 0; attempt < 100; ++attempt)
            {
                const auto candidate = utils::executableDirectory() / ("dlog-data-manager-tests-" + std::to_string(stamp) + "-" + std::to_string(attempt));
                std::error_code error;
                if (fs::create_directory(candidate, error))
                {
                    m_directory = candidate;
                    break;
                }
                ASSERT_FALSE(error) << error.message();
            }
            ASSERT_FALSE(m_directory.empty()) << "Cannot create an isolated temporary directory";

            m_filePath = m_directory / "metadata" / "storage.json";
            fs::create_directory(m_filePath.parent_path());
            writeRecords({});
            configureStorageFile(m_filePath);
        }

        void TearDown() override
        {
            std::error_code error;
            if (!m_originalWorkingDirectory.empty())
            {
                fs::current_path(m_originalWorkingDirectory, error);
                EXPECT_FALSE(error) << error.message();
            }

            // 复用 XMake 复制的配置，只替换 storage_info；结束后同时恢复文件和单例。
            if (!m_originalConfig.empty())
            {
                EXPECT_NO_THROW(utils::FileUtil(m_configFile).setContent(m_originalConfig));
                EXPECT_NO_THROW(server::Config::GetInstance().readConfig());
            }

            if (!m_directory.empty())
            {
                fs::remove_all(m_directory, error);
                EXPECT_FALSE(error) << error.message();
            }
        }

        void configureStorageFile(const fs::path &storageFile) const
        {
            auto root = utils::JsonUtil::UnSerialize(m_originalConfig);
            root["storage_info"] = storageFile.string();
            utils::FileUtil(m_configFile).setContent(Json::writeString(Json::StreamWriterBuilder{}, root));
            server::Config::GetInstance().readConfig();
        }

        void writeMetadata(const Json::Value &root) const
        {
            utils::FileUtil(m_filePath).setContent(Json::writeString(Json::StreamWriterBuilder{}, root));
        }

        void writeRecords(const std::vector<server::FileMetadata> &infos) const
        {
            Json::Value root(Json::arrayValue);
            for (const auto &info : infos) root.append(MakeRecord(info));
            writeMetadata(root);
        }

        Json::Value readMetadata() const
        {
            return utils::JsonUtil::UnSerialize(utils::FileUtil(m_filePath).getContent());
        }

        static server::FileMetadata MakeInfo(const std::string &name, size_t fileSize = 17, Json::Int64 mtime = -1234567890123LL)
        {
            server::FileMetadata info;
            info.m_mtime = fs::file_time_type(fs::file_time_type::duration(mtime));
            info.m_fileSize = fileSize;
            info.m_absoluteFilePath = utils::executableDirectory() / "./data" / name;
            info.m_url = "/download/" + name;
            return info;
        }

        static Json::Value MakeRecord(const server::FileMetadata &info)
        {
            Json::Value item;
            item["mtime"] = static_cast<Json::Int64>(info.m_mtime.time_since_epoch().count());
            item["file_size"] = static_cast<Json::UInt64>(info.m_fileSize);
            item["absolute_file_path"] = info.m_absoluteFilePath.string();
            item["url"] = info.m_url;
            return item;
        }

        static void ExpectInfo(const server::FileMetadata &actual, const server::FileMetadata &expected)
        {
            EXPECT_TRUE(actual.m_absoluteFilePath.is_absolute());
            EXPECT_EQ(actual.m_mtime, expected.m_mtime);
            EXPECT_EQ(actual.m_fileSize, expected.m_fileSize);
            EXPECT_EQ(actual.m_absoluteFilePath, expected.m_absoluteFilePath);
            EXPECT_EQ(actual.m_url, expected.m_url);
        }

        // unordered_map 不保证顺序，按 URL 对照记录，并比较全部字段。
        static void ExpectInfos(const std::vector<server::FileMetadata> &actual, const std::vector<server::FileMetadata> &expected)
        {
            ASSERT_EQ(actual.size(), expected.size());
            for (const auto &info : expected)
            {
                SCOPED_TRACE(info.m_url);
                const auto it = std::find_if(actual.begin(), actual.end(), [&info](const auto &candidate)
                                             { return candidate.m_url == info.m_url; });
                ASSERT_NE(it, actual.end());
                ExpectInfo(*it, info);
            }
        }

        fs::path m_configFile;
        fs::path m_directory;
        fs::path m_filePath;
        fs::path m_originalWorkingDirectory;
        std::string m_originalConfig;
    };
}


TEST(FileMetadataTests, CreatesMetadataFromFile)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto file = fs::temp_directory_path() / ("storage-info-" + std::to_string(stamp) + ".txt");
    ASSERT_FALSE(fs::exists(file));
    utils::FileUtil(file).setContent("hello");

    EXPECT_NO_THROW({
        const auto info = server::FileMetadata::FromFile(file);

        EXPECT_EQ(info.m_mtime, fs::last_write_time(file));
        EXPECT_EQ(info.m_fileSize, 5U);
        EXPECT_EQ(info.m_absoluteFilePath, file);
        EXPECT_EQ(info.m_url, server::Config::GetInstance().getDownloadPrefix() + file.filename().string());
    });

    fs::remove(file);
}

TEST(FileMetadataTests, RejectsMissingFile)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto file = fs::temp_directory_path() / ("storage-info-missing-" + std::to_string(stamp) + ".txt");

    EXPECT_THROW(server::FileMetadata::FromFile(file), fs::filesystem_error);
}

TEST_F(DataManagerTests, FileMetadataUsesExecutableDirectoryAfterWorkingDirectoryChanges)
{
    const auto file = m_directory / u8"报告 一.txt";
    const auto relativeFile = m_directory.filename() / file.filename();
    utils::FileUtil(file).setContent("hello");

    const auto workingDirectory = m_directory / "working";
    const auto decoy = workingDirectory / relativeFile;
    fs::create_directories(decoy.parent_path());
    utils::FileUtil(decoy).setContent("different content in the working directory");
    fs::current_path(workingDirectory);

    for (const auto &filePath : {relativeFile, file})
    {
        SCOPED_TRACE(filePath);
        const auto info = server::FileMetadata::FromFile(filePath);

        EXPECT_EQ(info.m_mtime, fs::last_write_time(file));
        EXPECT_EQ(info.m_fileSize, 5U);
        EXPECT_TRUE(info.m_absoluteFilePath.is_absolute());
        EXPECT_EQ(info.m_absoluteFilePath, file);
        EXPECT_EQ(info.m_url, server::Config::GetInstance().getDownloadPrefix() + u8"报告 一.txt");
    }
}

TEST_F(DataManagerTests, FileMetadataDoesNotFallBackToWorkingDirectory)
{
    const auto relativeFile = m_directory.filename() / "missing.txt";
    const auto workingDirectory = m_directory / "working";
    const auto decoy = workingDirectory / relativeFile;
    fs::create_directories(decoy.parent_path());
    utils::FileUtil(decoy).setContent("only exists in the working directory");
    ASSERT_FALSE(fs::exists(utils::executableDirectory() / relativeFile));
    fs::current_path(workingDirectory);

    EXPECT_THROW(server::FileMetadata::FromFile(relativeFile), fs::filesystem_error);
}


// 构造、启动加载和持久化文件校验。

TEST_F(DataManagerTests, ConstructorLoadsAllFieldsFromConfiguredFile)
{
    // 使用独立的 JSON 样例，避免准备数据的代码与被测代码同时写错字段。
    utils::FileUtil(m_filePath).setContent(R"([
        {"mtime": -1234567890123, "file_size": 17, "absolute_file_path": "/data/first.txt", "url": "/download/first.txt"},
        {"mtime": 9876543210123, "file_size": 4294967297, "absolute_file_path": "/data/second.txt", "url": "/download/second.txt"}
    ])");
    auto first = MakeInfo("first.txt");
    auto second = MakeInfo("second.txt", 4294967297ULL, 9876543210123LL);
    first.m_absoluteFilePath = "/data/first.txt";
    second.m_absoluteFilePath = "/data/second.txt";

    const server::DataManager manager;

    ExpectInfo(manager.getOneByUrl(first.m_url), first);
    ExpectInfo(manager.getOneByFilePath(second.m_absoluteFilePath), second);
    ExpectInfos(manager.getAll(), {first, second});
}

TEST_F(DataManagerTests, ConstructorAcceptsEmptyArray)
{
    const server::DataManager manager;

    EXPECT_TRUE(manager.getAll().empty());
    EXPECT_THROW(manager.getOneByUrl("/download/missing.txt"), std::out_of_range);
    EXPECT_THROW(manager.getOneByFilePath("./data/missing.txt"), std::out_of_range);
    EXPECT_TRUE(manager.getAll().empty());
}

TEST_F(DataManagerTests, ConstructorSupportsUtf8MetadataFilePath)
{
    const auto storageFile = m_directory / u8"元数据 文件.json";
    fs::rename(m_filePath, storageFile);
    m_filePath = storageFile;
    configureStorageFile(storageFile);
    const auto info = MakeInfo(u8"报告.txt");
    writeRecords({info});

    server::DataManager manager;
    ExpectInfos(manager.getAll(), {info});
    ASSERT_NO_THROW(manager.persistToDisk());

    const server::DataManager restored;
    ExpectInfos(restored.getAll(), {info});
}

TEST_F(DataManagerTests, ConstructionAndDestructionDoNotRewriteMetadata)
{
    const std::string body = R"([ { "mtime": -1234567890123, "file_size": 17, "absolute_file_path": "/data/report.txt", "url": "/download/report.txt", "future_field": true } ])";
    utils::FileUtil(m_filePath).setContent(body);
    const auto timestamp = fs::file_time_type::clock::now() - std::chrono::hours(24);
    fs::last_write_time(m_filePath, timestamp);
    const auto originalTime = fs::last_write_time(m_filePath);

    {
        const server::DataManager manager;
        auto info = MakeInfo("report.txt");
        info.m_absoluteFilePath = "/data/report.txt";
        ExpectInfos(manager.getAll(), {info});
        EXPECT_EQ(utils::FileUtil(m_filePath).getContent(), body);
        EXPECT_EQ(fs::last_write_time(m_filePath), originalTime);
    }

    EXPECT_EQ(utils::FileUtil(m_filePath).getContent(), body);
    EXPECT_EQ(fs::last_write_time(m_filePath), originalTime);
}

TEST_F(DataManagerTests, ConstructorRejectsMissingMetadataFile)
{
    fs::remove(m_filePath);

    try
    {
        server::DataManager manager;
        FAIL() << "Construction must fail when metadata is missing";
    }
    catch (const fs::filesystem_error &error)
    {
        EXPECT_EQ(error.code(), std::make_error_code(std::errc::no_such_file_or_directory));
        EXPECT_EQ(error.path1(), m_filePath);
    }

    EXPECT_FALSE(fs::exists(m_filePath));
}

TEST_F(DataManagerTests, ConstructorRejectsDirectoryAtMetadataPath)
{
    fs::remove(m_filePath);
    fs::create_directory(m_filePath);

    EXPECT_THROW(server::DataManager{}, fs::filesystem_error);
    EXPECT_TRUE(fs::is_directory(m_filePath));
}

TEST_F(DataManagerTests, ConstructorReportsUnreadableMetadata)
{
    fs::permissions(m_filePath, fs::perms::none);
    std::ifstream probe(m_filePath, std::ios::binary);
    if (probe.is_open()) GTEST_SKIP() << "The current user can read files without read permission";

    EXPECT_THROW(server::DataManager{}, std::ios_base::failure);
}

TEST_F(DataManagerTests, ConstructorRejectsMalformedOrNonArrayDocuments)
{
    for (const char *body : {"", " \t\r\n", "[", "{", "[{\"mtime\":}]", "null", "{}", "true", "false", "42", "1.5", "\"metadata\""})
    {
        SCOPED_TRACE(body);
        utils::FileUtil(m_filePath).setContent(body);

        EXPECT_THROW(server::DataManager{}, std::invalid_argument);
        EXPECT_EQ(utils::FileUtil(m_filePath).getContent(), body);
    }
}

TEST_F(DataManagerTests, ConstructorRejectsNonObjectRecords)
{
    for (const auto &value : {Json::Value(), Json::Value(false), Json::Value(42), Json::Value("record"), Json::Value(Json::arrayValue)})
    {
        SCOPED_TRACE(Json::writeString(Json::StreamWriterBuilder{}, value));
        Json::Value root(Json::arrayValue);
        root.append(value);
        writeMetadata(root);

        EXPECT_THROW(server::DataManager{}, std::invalid_argument);
    }
}

TEST_F(DataManagerTests, ConstructorRejectsMissingFields)
{
    for (const char *field : {"mtime", "file_size", "absolute_file_path", "url"})
    {
        SCOPED_TRACE(field);
        auto item = MakeRecord(MakeInfo("report.txt"));
        item.removeMember(field);
        Json::Value root(Json::arrayValue);
        root.append(item);
        writeMetadata(root);

        EXPECT_THROW(server::DataManager{}, std::invalid_argument);
    }
}

TEST_F(DataManagerTests, ConstructorRejectsInvalidFieldTypes)
{
    for (const char *field : {"mtime", "file_size", "absolute_file_path", "url"})
    {
        SCOPED_TRACE(field);
        const bool numeric = std::string(field) == "mtime" || std::string(field) == "file_size";
        const std::vector<Json::Value> invalidValues = {
            Json::Value(), Json::Value(false), Json::Value(true), Json::Value(0.5),
            Json::Value(Json::arrayValue), Json::Value(Json::objectValue),
            numeric ? Json::Value("17") : Json::Value(17)};

        for (const auto &value : invalidValues)
        {
            SCOPED_TRACE(Json::writeString(Json::StreamWriterBuilder{}, value));
            auto item = MakeRecord(MakeInfo("report.txt"));
            item[field] = value;
            Json::Value root(Json::arrayValue);
            root.append(item);
            writeMetadata(root);

            EXPECT_THROW(server::DataManager{}, std::invalid_argument);
        }
    }
}

TEST_F(DataManagerTests, ConstructorRejectsOutOfRangeNumbers)
{
    struct Case
    {
        const char *m_field;
        Json::Value m_value;
    };
    const std::vector<Case> cases = {
        {"mtime", Json::Value(std::numeric_limits<Json::UInt64>::max())},
        {"mtime", Json::Value(-1e100)},
        {"mtime", Json::Value(1e100)},
        {"file_size", Json::Value(-1)},
        {"file_size", Json::Value(std::numeric_limits<Json::Int64>::min())},
        {"file_size", Json::Value(1e100)}};

    for (const auto &testCase : cases)
    {
        SCOPED_TRACE(testCase.m_field);
        SCOPED_TRACE(Json::writeString(Json::StreamWriterBuilder{}, testCase.m_value));
        auto item = MakeRecord(MakeInfo("report.txt"));
        item[testCase.m_field] = testCase.m_value;
        Json::Value root(Json::arrayValue);
        root.append(item);
        writeMetadata(root);

        EXPECT_THROW(server::DataManager{}, std::invalid_argument);
    }
}

TEST_F(DataManagerTests, ConstructorRejectsDuplicateUrls)
{
    const auto original = MakeInfo("report.txt");
    auto duplicate = MakeInfo("other.txt", 99, 123);
    duplicate.m_url = original.m_url;
    writeRecords({original, duplicate});
    const auto body = utils::FileUtil(m_filePath).getContent();

    EXPECT_THROW(server::DataManager{}, std::invalid_argument);
    EXPECT_EQ(utils::FileUtil(m_filePath).getContent(), body);
}

TEST_F(DataManagerTests, ConstructorCanBeRetriedAfterInvalidRecord)
{
    const auto first = MakeInfo("first.txt");
    const auto second = MakeInfo("second.txt", 23, 456);
    Json::Value root(Json::arrayValue);
    root.append(MakeRecord(first));
    root.append(Json::Value(Json::objectValue));
    writeMetadata(root);
    EXPECT_THROW(server::DataManager{}, std::invalid_argument);

    writeRecords({first, second});
    const server::DataManager manager;
    ExpectInfos(manager.getAll(), {first, second});
}

TEST_F(DataManagerTests, RelativeMetadataPathUsesExecutableDirectoryForLoadingAndPersistence)
{
    const auto relativeFile = m_directory.filename() / "metadata" / "storage.json";
    configureStorageFile(relativeFile);
    const auto info = MakeInfo("report.txt");
    writeRecords({info});

    const auto workingDirectory = m_directory / "working";
    const auto decoy = workingDirectory / relativeFile;
    fs::create_directories(decoy.parent_path());
    utils::FileUtil(decoy).setContent("[]");
    fs::current_path(workingDirectory);

    server::DataManager manager;
    ExpectInfos(manager.getAll(), {info});

    const auto otherDirectory = m_directory / "other-working";
    fs::create_directory(otherDirectory);
    fs::current_path(otherDirectory);
    const auto added = MakeInfo("added.txt");
    ASSERT_NO_THROW(manager.insert(added));

    const server::DataManager restored;
    ExpectInfos(restored.getAll(), {info, added});
    EXPECT_EQ(utils::FileUtil(decoy).getContent(), "[]");
    EXPECT_FALSE(fs::exists(otherDirectory / relativeFile));
}

TEST_F(DataManagerTests, ConstructorDoesNotFallBackToMetadataInWorkingDirectory)
{
    const auto relativeFile = m_directory.filename() / "missing.json";
    configureStorageFile(relativeFile);
    const auto workingDirectory = m_directory / "working";
    const auto decoy = workingDirectory / relativeFile;
    fs::create_directories(decoy.parent_path());
    utils::FileUtil(decoy).setContent("[]");
    ASSERT_FALSE(fs::exists(utils::executableDirectory() / relativeFile));
    fs::current_path(workingDirectory);

    EXPECT_THROW(server::DataManager{}, fs::filesystem_error);
    EXPECT_EQ(utils::FileUtil(decoy).getContent(), "[]");
}

TEST_F(DataManagerTests, LoadFromDiskRestoresRecordsWithoutPersisting)
{
    server::DataManager manager;
    const auto info = MakeInfo("report.txt");
    writeRecords({info});
    const auto body = utils::FileUtil(m_filePath).getContent();
    fs::last_write_time(m_filePath, fs::file_time_type::clock::now() - std::chrono::hours(24));
    const auto timestamp = fs::last_write_time(m_filePath);

    ASSERT_NO_THROW(manager.loadFromDisk());

    ExpectInfos(manager.getAll(), {info});
    EXPECT_EQ(utils::FileUtil(m_filePath).getContent(), body);
    EXPECT_EQ(fs::last_write_time(m_filePath), timestamp);
}

TEST_F(DataManagerTests, LoadFromDiskReportsErrorsAndCanBeRetried)
{
    server::DataManager manager;
    fs::remove(m_filePath);
    EXPECT_THROW(manager.loadFromDisk(), fs::filesystem_error);
    EXPECT_TRUE(manager.getAll().empty());

    utils::FileUtil(m_filePath).setContent("[");
    EXPECT_THROW(manager.loadFromDisk(), std::invalid_argument);
    EXPECT_TRUE(manager.getAll().empty());

    const auto info = MakeInfo("report.txt");
    writeRecords({info});
    ASSERT_NO_THROW(manager.loadFromDisk());
    ExpectInfos(manager.getAll(), {info});
}

TEST_F(DataManagerTests, LoadFromDiskPreservesAbsolutePathsAfterWorkingDirectoryChanges)
{
    const auto info = MakeInfo("report.txt");
    writeRecords({info});
    fs::current_path(m_directory);

    server::DataManager manager;
    ExpectInfo(manager.getOneByUrl(info.m_url), info);
    ExpectInfo(manager.getOneByFilePath("./data/report.txt"), info);

    ASSERT_NO_THROW(manager.persistToDisk());
    EXPECT_EQ(readMetadata()[0]["absolute_file_path"].asString(), info.m_absoluteFilePath.string());
}

TEST_F(DataManagerTests, LoadFromDiskRejectsUrlsAlreadyInMemory)
{
    const auto info = MakeInfo("report.txt");
    writeRecords({info});
    server::DataManager manager;
    const auto body = utils::FileUtil(m_filePath).getContent();

    EXPECT_THROW(manager.loadFromDisk(), std::invalid_argument);

    ExpectInfos(manager.getAll(), {info});
    EXPECT_EQ(utils::FileUtil(m_filePath).getContent(), body);
}


// 查询返回副本，不改变内存表或持久化文件。

TEST_F(DataManagerTests, GetAllReturnsEveryRecordWithoutDependingOnOrder)
{
    const std::vector<server::FileMetadata> infos = {
        MakeInfo("third.txt", 0, 0), MakeInfo("first.txt", 31, -9876543210123LL), MakeInfo("second.txt", 72, 9876543210123LL)};
    writeRecords(infos);
    const server::DataManager manager;

    ExpectInfos(manager.getAll(), infos);
    for (const auto &info : infos)
    {
        ExpectInfo(manager.getOneByUrl(info.m_url), info);
        ExpectInfo(manager.getOneByFilePath(info.m_absoluteFilePath), info);
    }
}

TEST_F(DataManagerTests, QueriesReturnIndependentCopies)
{
    const auto info = MakeInfo("report.txt");
    writeRecords({info});
    const server::DataManager manager;
    const auto body = utils::FileUtil(m_filePath).getContent();

    auto byUrl = manager.getOneByUrl(info.m_url);
    auto byPath = manager.getOneByFilePath(info.m_absoluteFilePath);
    auto all = manager.getAll();
    byUrl.m_fileSize = 999;
    byPath.m_absoluteFilePath = "changed";
    ASSERT_EQ(all.size(), 1U);
    all.front().m_url = "/changed/";
    all.front().m_mtime = fs::file_time_type{};
    all.clear();

    ExpectInfo(manager.getOneByUrl(info.m_url), info);
    ExpectInfo(manager.getOneByFilePath(info.m_absoluteFilePath), info);
    ExpectInfos(manager.getAll(), {info});
    EXPECT_EQ(utils::FileUtil(m_filePath).getContent(), body);
}

TEST_F(DataManagerTests, GetOneByFilePathComparesPathComponents)
{
    auto info = MakeInfo("report.txt");
    info.m_absoluteFilePath = utils::executableDirectory() / "./data" / "folder" / "report.txt";
    writeRecords({info});
    const server::DataManager manager;
    const auto body = utils::FileUtil(m_filePath).getContent();

    const std::vector<fs::path> paths = {
        "./data//folder///report.txt",
        "./data/folder/report.txt",
        info.m_absoluteFilePath};
    for (const auto &filePath : paths)
    {
        SCOPED_TRACE(filePath);
        ExpectInfo(manager.getOneByFilePath(filePath), info);
    }

    ExpectInfo(manager.getOneByUrl(info.m_url), info);
    EXPECT_EQ(utils::FileUtil(m_filePath).getContent(), body);
}

TEST_F(DataManagerTests, GetOneByFilePathMatchesAbsoluteRecordUsingRelativePath)
{
    auto info = MakeInfo("report.txt");
    info.m_absoluteFilePath = utils::executableDirectory() / "data" / "report.txt";
    writeRecords({info});
    const server::DataManager manager;

    ExpectInfo(manager.getOneByFilePath("data/report.txt"), info);
    ExpectInfo(manager.getOneByFilePath(info.m_absoluteFilePath), info);
}

TEST_F(DataManagerTests, GetOneByFilePathUsesExecutableDirectoryAfterWorkingDirectoryChanges)
{
    const auto relativeFile = m_directory.filename() / "files" / "report.txt";
    auto info = MakeInfo("report.txt");
    info.m_absoluteFilePath = utils::executableDirectory() / relativeFile;
    auto decoy = MakeInfo("decoy.txt");
    decoy.m_absoluteFilePath = m_directory / relativeFile;
    ASSERT_FALSE(fs::exists(info.m_absoluteFilePath));
    ASSERT_FALSE(fs::exists(decoy.m_absoluteFilePath));
    writeRecords({info, decoy});
    const server::DataManager manager;
    fs::current_path(m_directory);

    ExpectInfo(manager.getOneByFilePath(relativeFile), info);
    ExpectInfo(manager.getOneByFilePath(info.m_absoluteFilePath), info);
    ExpectInfo(manager.getOneByFilePath(decoy.m_absoluteFilePath), decoy);
}

TEST_F(DataManagerTests, MissingQueriesDoNotInsertRecords)
{
    const auto info = MakeInfo("report.txt");
    writeRecords({info});
    server::DataManager manager;
    const auto body = utils::FileUtil(m_filePath).getContent();

    for (const char *url : {"", "/download/missing.txt", "/download/REPORT.txt", "report.txt", "/download/report.txt?x=1"})
    {
        SCOPED_TRACE(url);
        EXPECT_THROW(manager.getOneByUrl(url), std::out_of_range);
    }
    for (const char *path : {"", "./data/missing.txt", "./data/REPORT.txt", "./other/report.txt", "./data/../other/report.txt"})
    {
        SCOPED_TRACE(path);
        EXPECT_THROW(manager.getOneByFilePath(path), std::out_of_range);
    }

    ExpectInfos(manager.getAll(), {info});
    EXPECT_EQ(utils::FileUtil(m_filePath).getContent(), body);

    // 查询抛出异常后，读锁应当释放，后续仍可写入。
    const auto added = MakeInfo("added.txt");
    ASSERT_NO_THROW(manager.insert(added));
    ExpectInfos(manager.getAll(), {info, added});
}


// insert 只新增，update 只更新已有 URL；成功后应立即保存整张表。

TEST_F(DataManagerTests, InsertAddsRecordAndImmediatelyPersistsWholeTable)
{
    const auto first = MakeInfo("first.txt");
    const auto second = MakeInfo("second.txt", 41, 678);
    const auto added = MakeInfo("added.txt", 0, 0);
    writeRecords({first, second});
    server::DataManager manager;

    ASSERT_NO_THROW(manager.insert(added));

    ExpectInfos(manager.getAll(), {first, second, added});
    const server::DataManager restored;
    ExpectInfos(restored.getAll(), {first, second, added});
}

TEST_F(DataManagerTests, InsertRejectsDuplicateUrlWithoutChangingMemoryOrDisk)
{
    const auto original = MakeInfo("report.txt");
    writeRecords({original});
    server::DataManager manager;
    auto duplicate = MakeInfo("other.txt", 99, 123);
    duplicate.m_url = original.m_url;
    const auto body = utils::FileUtil(m_filePath).getContent();

    EXPECT_THROW(manager.insert(duplicate), std::invalid_argument);

    ExpectInfos(manager.getAll(), {original});
    EXPECT_EQ(utils::FileUtil(m_filePath).getContent(), body);

    const auto added = MakeInfo("added.txt");
    ASSERT_NO_THROW(manager.insert(added));
    const server::DataManager restored;
    ExpectInfos(restored.getAll(), {original, added});
}

TEST_F(DataManagerTests, InsertUsesUrlAsUniqueKey)
{
    const auto first = MakeInfo("first.txt");
    auto second = MakeInfo("second.txt");
    second.m_absoluteFilePath = first.m_absoluteFilePath;
    writeRecords({first});
    server::DataManager manager;

    ASSERT_NO_THROW(manager.insert(second));

    ExpectInfo(manager.getOneByUrl(first.m_url), first);
    ExpectInfo(manager.getOneByUrl(second.m_url), second);
    const server::DataManager restored;
    ExpectInfos(restored.getAll(), {first, second});
}

TEST_F(DataManagerTests, UpdateReplacesRecordAndImmediatelyPersistsWholeTable)
{
    const auto original = MakeInfo("report.txt");
    const auto other = MakeInfo("other.txt", 41, 678);
    writeRecords({original, other});
    server::DataManager manager;
    auto updated = MakeInfo("new-location.txt", 987, 1234567890123LL);
    updated.m_url = original.m_url;

    ASSERT_NO_THROW(manager.update(updated));

    ExpectInfo(manager.getOneByUrl(original.m_url), updated);
    ExpectInfo(manager.getOneByFilePath(updated.m_absoluteFilePath), updated);
    EXPECT_THROW(manager.getOneByFilePath(original.m_absoluteFilePath), std::out_of_range);
    ExpectInfos(manager.getAll(), {updated, other});
    const server::DataManager restored;
    ExpectInfos(restored.getAll(), {updated, other});
}

TEST_F(DataManagerTests, UpdateRejectsUnknownUrlWithoutChangingMemoryOrDisk)
{
    const auto original = MakeInfo("report.txt");
    writeRecords({original});
    server::DataManager manager;
    const auto missing = MakeInfo("missing.txt");
    const auto body = utils::FileUtil(m_filePath).getContent();

    EXPECT_THROW(manager.update(missing), std::out_of_range);

    ExpectInfos(manager.getAll(), {original});
    EXPECT_THROW(manager.getOneByUrl(missing.m_url), std::out_of_range);
    EXPECT_EQ(utils::FileUtil(m_filePath).getContent(), body);

    auto updated = original;
    updated.m_fileSize = 0;
    ASSERT_NO_THROW(manager.update(updated));
    const server::DataManager restored;
    ExpectInfos(restored.getAll(), {updated});
}


// JSON 输出格式、完整覆盖、数值精度及重建后的数据往返。

TEST_F(DataManagerTests, PersistToDiskWritesEmptyArray)
{
    server::DataManager manager;

    ASSERT_NO_THROW(manager.persistToDisk());

    const auto root = readMetadata();
    ASSERT_TRUE(root.isArray());
    EXPECT_TRUE(root.empty());
    const server::DataManager restored;
    EXPECT_TRUE(restored.getAll().empty());
}

TEST_F(DataManagerTests, PersistToDiskWritesExactSchemaAndValues)
{
    const auto info = MakeInfo("report.txt", 4294967297ULL, -1234567890123LL);
    writeRecords({info});
    server::DataManager manager;

    ASSERT_NO_THROW(manager.persistToDisk());

    const auto root = readMetadata();
    ASSERT_TRUE(root.isArray());
    ASSERT_EQ(root.size(), 1U);
    const auto &item = root[0];
    ASSERT_TRUE(item.isObject());
    ASSERT_EQ(item.size(), 4U);
    ASSERT_TRUE(item["mtime"].isInt64());
    ASSERT_TRUE(item["file_size"].isUInt64());
    ASSERT_TRUE(item["absolute_file_path"].isString());
    ASSERT_TRUE(item["url"].isString());
    EXPECT_EQ(item["mtime"].asInt64(), -1234567890123LL);
    EXPECT_EQ(item["file_size"].asUInt64(), 4294967297ULL);
    EXPECT_EQ(item["absolute_file_path"].asString(), info.m_absoluteFilePath.string());
    EXPECT_TRUE(fs::path(item["absolute_file_path"].asString()).is_absolute());
    EXPECT_EQ(item["url"].asString(), "/download/report.txt");
}

TEST_F(DataManagerTests, PersistToDiskReplacesLongerContentsWithoutTrailingBytes)
{
    auto info = MakeInfo("report.txt");
    info.m_absoluteFilePath = utils::executableDirectory() / std::string(8192, 'x');
    writeRecords({info});
    const auto originalSize = fs::file_size(m_filePath);
    server::DataManager manager;
    info.m_absoluteFilePath = utils::executableDirectory() / "a";

    ASSERT_NO_THROW(manager.update(info));

    EXPECT_LT(fs::file_size(m_filePath), originalSize);
    EXPECT_NO_THROW(readMetadata());
    const server::DataManager restored;
    ExpectInfos(restored.getAll(), {info});
}

TEST_F(DataManagerTests, MetadataRoundTripPreservesNumericLimits)
{
    using Rep = fs::file_time_type::duration::rep;
    const std::vector<server::FileMetadata> infos = {
        MakeInfo("oldest.txt", 0, std::numeric_limits<Rep>::min()),
        MakeInfo("newest.txt", std::numeric_limits<size_t>::max(), std::numeric_limits<Rep>::max()),
        MakeInfo("epoch.txt", 1, 0)};
    writeRecords(infos);
    server::DataManager manager;
    ExpectInfos(manager.getAll(), infos);

    ASSERT_NO_THROW(manager.persistToDisk());

    const server::DataManager restored;
    ExpectInfos(restored.getAll(), infos);
}

TEST_F(DataManagerTests, MetadataRoundTripPreservesUnicodeAndEscapedStrings)
{
    auto info = MakeInfo(u8"报告 \"一\"+\\换行\n.txt");
    const auto filePath = m_directory.string() + u8"/不存在的 文件 \"一\"+\\换行\n.txt";
    info.m_absoluteFilePath = filePath;
    ASSERT_FALSE(fs::exists(info.m_absoluteFilePath));
    server::DataManager manager;

    ASSERT_NO_THROW(manager.insert(info));

    const auto root = readMetadata();
    ASSERT_TRUE(root.isArray());
    ASSERT_EQ(root.size(), 1U);
    EXPECT_EQ(root[0]["absolute_file_path"].asString(), filePath);

    const server::DataManager restored;
    ExpectInfo(restored.getOneByUrl(info.m_url), info);
    ExpectInfo(restored.getOneByFilePath(info.m_absoluteFilePath), info);
    EXPECT_FALSE(fs::exists(info.m_absoluteFilePath));
}

TEST_F(DataManagerTests, PersistToDiskRecreatesRemovedMetadataFile)
{
    const auto info = MakeInfo("report.txt");
    writeRecords({info});
    server::DataManager manager;
    fs::remove(m_filePath);

    ASSERT_NO_THROW(manager.persistToDisk());

    const server::DataManager restored;
    ExpectInfos(restored.getAll(), {info});
}

TEST_F(DataManagerTests, InstancesKeepTheirOwnConfiguredFilePath)
{
    const auto info = MakeInfo("report.txt");
    writeRecords({info});
    server::DataManager manager;
    const auto otherFile = m_directory / "other.json";
    utils::FileUtil(otherFile).setContent("[]");
    configureStorageFile(otherFile);

    const auto added = MakeInfo("added.txt");
    ASSERT_NO_THROW(manager.insert(added));

    const server::DataManager other;
    EXPECT_TRUE(other.getAll().empty());
    EXPECT_EQ(utils::FileUtil(otherFile).getContent(), "[]");
    configureStorageFile(m_filePath);
    const server::DataManager restored;
    ExpectInfos(restored.getAll(), {info, added});
}


// 写盘失败要向调用方报告，恢复文件路径后可以重试保存。

TEST_F(DataManagerTests, PersistToDiskReportsMissingParentAndCanBeRetried)
{
    const auto info = MakeInfo("report.txt");
    writeRecords({info});
    server::DataManager manager;
    fs::remove_all(m_filePath.parent_path());

    EXPECT_THROW(manager.persistToDisk(), std::ios_base::failure);
    ExpectInfos(manager.getAll(), {info});

    fs::create_directory(m_filePath.parent_path());
    ASSERT_NO_THROW(manager.persistToDisk());
    const server::DataManager restored;
    ExpectInfos(restored.getAll(), {info});
}

TEST_F(DataManagerTests, PersistToDiskReportsDirectoryAtFilePathAndCanBeRetried)
{
    const auto info = MakeInfo("report.txt");
    writeRecords({info});
    server::DataManager manager;
    fs::remove(m_filePath);
    fs::create_directory(m_filePath);

    EXPECT_THROW(manager.persistToDisk(), std::ios_base::failure);
    ExpectInfos(manager.getAll(), {info});

    fs::remove(m_filePath);
    ASSERT_NO_THROW(manager.persistToDisk());
    const server::DataManager restored;
    ExpectInfos(restored.getAll(), {info});
}

TEST_F(DataManagerTests, InsertReportsPersistenceFailureAndAllowsRetry)
{
    const auto original = MakeInfo("original.txt");
    const auto added = MakeInfo("added.txt");
    writeRecords({original});
    server::DataManager manager;
    fs::remove(m_filePath);
    fs::create_directory(m_filePath);

    EXPECT_THROW(manager.insert(added), std::ios_base::failure);

    // 当前实现先修改内存再写盘；保存失败不会回滚，可通过 persistToDisk() 重试。
    ExpectInfos(manager.getAll(), {original, added});
    fs::remove(m_filePath);
    ASSERT_NO_THROW(manager.persistToDisk());
    const server::DataManager restored;
    ExpectInfos(restored.getAll(), {original, added});
}

TEST_F(DataManagerTests, UpdateReportsPersistenceFailureAndAllowsRetry)
{
    const auto original = MakeInfo("report.txt");
    const auto other = MakeInfo("other.txt");
    writeRecords({original, other});
    server::DataManager manager;
    auto updated = original;
    updated.m_fileSize = 789;
    updated.m_mtime = fs::file_time_type(fs::file_time_type::duration(456));
    fs::remove(m_filePath);
    fs::create_directory(m_filePath);

    EXPECT_THROW(manager.update(updated), std::ios_base::failure);

    ExpectInfos(manager.getAll(), {updated, other});
    fs::remove(m_filePath);
    ASSERT_NO_THROW(manager.persistToDisk());
    const server::DataManager restored;
    ExpectInfos(restored.getAll(), {updated, other});
}


// 一个写线程与多个读线程同时访问，返回的每份元数据都应完整一致。

TEST_F(DataManagerTests, ConcurrentQueriesObserveConsistentRecordsDuringUpdates)
{
    const auto initial = MakeInfo("report.txt", 0, 1000);
    writeRecords({initial});
    server::DataManager manager;
    std::promise<void> start;
    const auto ready = start.get_future().share();
    std::vector<std::future<bool>> readers;

    for (int reader = 0; reader < 4; ++reader)
    {
        readers.push_back(std::async(std::launch::async, [&manager, &initial, ready]
                                     {
            ready.wait();
            for (int attempt = 0; attempt < 200; ++attempt)
            {
                const auto byUrl = manager.getOneByUrl(initial.m_url);
                const auto byPath = manager.getOneByFilePath(initial.m_absoluteFilePath);
                const auto all = manager.getAll();
                if (all.size() != 1) return false;

                // 各次查询可能处于不同版本；每份副本内部的字段必须来自同一个版本。
                for (const auto &info : {byUrl, byPath, all.front()})
                {
                    if (info.m_url != initial.m_url || info.m_absoluteFilePath != initial.m_absoluteFilePath ||
                        info.m_fileSize > 20 || info.m_mtime.time_since_epoch().count() != 1000 + static_cast<Json::Int64>(info.m_fileSize)) return false;
                }
            }
            return true; }));
    }

    start.set_value();
    for (size_t version = 1; version <= 20; ++version)
    {
        manager.update(MakeInfo("report.txt", version, 1000 + static_cast<Json::Int64>(version)));
    }
    for (auto &reader : readers) EXPECT_TRUE(reader.get());

    const server::DataManager restored;
    ExpectInfos(restored.getAll(), {MakeInfo("report.txt", 20, 1020)});
}
