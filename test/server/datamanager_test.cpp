#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>

#include "server/config.h"
#include "server/datamanager.h"
#include "utils/fileutil.h"


namespace fs = std::filesystem;


TEST(StorageInfoTests, CreatesMetadataFromFile)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto file = fs::temp_directory_path() / ("storage-info-" + std::to_string(stamp) + ".txt");
    ASSERT_FALSE(fs::exists(file));
    utils::FileUtil(file).setContent("hello");

    EXPECT_NO_THROW({
        const auto info = server::StorageInfo::fromFile(file.string());

        EXPECT_EQ(info.m_mtime, fs::last_write_time(file));
        EXPECT_EQ(info.m_fileSize, 5U);
        EXPECT_EQ(info.m_storagePath, file.string());
        EXPECT_EQ(info.m_url, server::Config::GetInstance().getDownloadPrefix() + file.filename().string());
    });

    fs::remove(file);
}

TEST(StorageInfoTests, RejectsMissingFile)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto file = fs::temp_directory_path() / ("storage-info-missing-" + std::to_string(stamp) + ".txt");

    EXPECT_THROW(server::StorageInfo::fromFile(file.string()), fs::filesystem_error);
}
