#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <bundle.h>

#include "fileutil.h"


namespace
{

    using utils::FileUtil;

    namespace fs = std::filesystem;

    class FileUtilTests : public ::testing::Test
    {

    protected:

        void SetUp() override
        {
            const auto parent = fs::temp_directory_path();
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();

            // 用创建目录的结果确认名称没有被占用，允许多个测试进程同时运行。
            for (int attempt = 0; attempt < 100; ++attempt)
            {
                const auto candidate = parent / ("dlog-file-util-tests-" + std::to_string(stamp) + "-" + std::to_string(attempt));
                std::error_code error;
                if (fs::create_directory(candidate, error))
                {
                    m_directory = candidate;
                    return;
                }
                ASSERT_FALSE(error) << error.message();
            }
            FAIL() << "Cannot create an isolated temporary directory";
        }

        void TearDown() override
        {
            if (m_directory.empty()) return;

            std::error_code error;
            fs::remove_all(m_directory, error);
            EXPECT_FALSE(error) << error.message();
        }

        fs::path path(std::string_view name) const { return m_directory / fs::u8path(name.begin(), name.end()); }

        bool canCreateSymlinks() const
        {
            // Windows 等环境可能未授予符号链接权限；先探测能力，再执行相关用例。
            const auto probe = path("symlink-probe");
            std::error_code error;
            fs::create_symlink("missing.txt", probe, error);
            if (error) return false;
            fs::remove(probe);

            fs::create_directory_symlink(".", probe, error);
            if (error) return false;
            fs::remove(probe);
            return true;
        }

        // 直接用标准文件流准备输入和检查输出，避免读写接口同时出错时相互掩盖。
        static void writeFile(const fs::path &file, std::string_view content)
        {
            std::ofstream stream;
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            stream.open(file, std::ios::binary | std::ios::trunc);
            if (!content.empty()) stream.write(content.data(), static_cast<std::streamsize>(content.size()));
            stream.close();
        }

        static std::string readFile(const fs::path &file)
        {
            std::ifstream stream;
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            stream.open(file, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        }

        fs::path m_directory;
    };

    // 即使断言提前返回，也恢复权限，保证临时目录能够正常清理。
    class ScopedPermissions
    {

    public:

        ScopedPermissions(const fs::path &path, fs::perms permissions)
            : m_path(path), m_original(fs::status(path).permissions())
        {
            fs::permissions(m_path, permissions);
        }

        ~ScopedPermissions()
        {
            std::error_code error;
            fs::permissions(m_path, m_original, error);
        }

        ScopedPermissions(const ScopedPermissions &) = delete;
        ScopedPermissions &operator=(const ScopedPermissions &) = delete;

    private:

        fs::path m_path;
        fs::perms m_original;
    };

    std::string compressionPayload()
    {
        std::string content;
        for (int i = 0; i < 1024; ++i) content += u8"cloud storage 压缩内容\n";
        return content;
    }

} // namespace


// 文件属性、读写和目录操作。

TEST_F(FileUtilTests, ConstructorKeepsItsOwnPath)
{
    const auto original = path("original.txt");
    writeFile(original, "original");
    writeFile(path("other.txt"), "other");
    auto supplied = original;
    const FileUtil file(supplied);
    supplied = path("other.txt");

    EXPECT_EQ(file.fileName(), "original.txt");
    EXPECT_EQ(file.getContent(), "original");
}

TEST_F(FileUtilTests, FileSizeCountsBytesIncludingNulsAndUtf8)
{
    const std::string content = std::string("a\0b\r\n", 5) + u8"云存储";
    writeFile(path("binary.dat"), content);

    EXPECT_EQ(FileUtil(path("binary.dat")).fileSize(), content.size());
}

TEST_F(FileUtilTests, EmptyFileHasZeroSizeAndEmptyContent)
{
    writeFile(path("empty.dat"), "");
    const FileUtil file(path("empty.dat"));

    EXPECT_EQ(file.fileSize(), 0U);
    EXPECT_TRUE(file.getContent().empty());
    EXPECT_TRUE(file.getPosLen(0, 0).empty());
}

TEST_F(FileUtilTests, FileSizeRejectsMissingFilesAndDirectories)
{
    EXPECT_THROW(FileUtil(path("missing.dat")).fileSize(), fs::filesystem_error);
    EXPECT_THROW(FileUtil(m_directory).fileSize(), fs::filesystem_error);
}

TEST(FileUtilPathTests, FileNameUsesOnlyTheLastPathComponent)
{
    const struct
    {
        fs::path input;
        fs::path expected;
    } cases[] = {
        {"", ""}, {"report.txt", "report.txt"}, {"data/report.tar.gz", "report.tar.gz"}, {"/missing/directory/report.txt", "report.txt"}, {"data/", ""}, {"/", ""}, {".", "."}, {"..", ".."}, {fs::u8path(u8"不存在/云 存储.txt"), fs::u8path(u8"云 存储.txt")}};

    for (const auto &item : cases)
    {
        SCOPED_TRACE(item.input.u8string());
        EXPECT_EQ(FileUtil(item.input).fileName(), item.expected);
    }
}

TEST_F(FileUtilTests, LastModifyTimeReadsTheStoredTimeOnEachCall)
{
    writeFile(path("time.txt"), "data");
    const FileUtil file(path("time.txt"));
    const auto expected = fs::file_time_type::clock::now() - std::chrono::hours(24);
    fs::last_write_time(path("time.txt"), expected);
    const auto first = file.lastModifyTime();

    // 文件系统可能舍入时间精度；显式设置时间，避免依赖 sleep() 等待文件发生变化。
    EXPECT_GE(first, expected - std::chrono::seconds(1));
    EXPECT_LE(first, expected + std::chrono::seconds(1));

    const auto earlier = expected - std::chrono::hours(24);
    fs::last_write_time(path("time.txt"), earlier);
    const auto second = file.lastModifyTime();
    EXPECT_GE(second, earlier - std::chrono::seconds(1));
    EXPECT_LE(second, earlier + std::chrono::seconds(1));
    EXPECT_LT(second, first);
}

TEST_F(FileUtilTests, LastModifyTimeRejectsMissingFiles)
{
    EXPECT_THROW(FileUtil(path("missing.txt")).lastModifyTime(), fs::filesystem_error);
}

TEST_F(FileUtilTests, GetContentPreservesEveryByteAndLineEnding)
{
    std::string content;
    for (unsigned int byte = 0; byte < 256; ++byte) content.push_back(static_cast<char>(byte));
    content += "\r\nline one\nline two\r\n";
    content += u8"云存储";
    writeFile(path(u8"含 空格的文件.dat"), content);

    EXPECT_EQ(FileUtil(path(u8"含 空格的文件.dat")).getContent(), content);
}

TEST_F(FileUtilTests, ReadsAndWritesHandleDataAcrossStreamBufferBoundaries)
{
    std::string content;
    for (size_t i = 0; i < 65537; ++i) content.push_back(static_cast<char>(i % 256));
    writeFile(path("input.dat"), content);

    const FileUtil input(path("input.dat"));
    EXPECT_EQ(input.getContent(), content);
    EXPECT_EQ(input.getPosLen(4090, 8193), content.substr(4090, 8193));

    FileUtil(path("output.dat")).setContent(content);
    EXPECT_EQ(readFile(path("output.dat")), content);
}

TEST_F(FileUtilTests, GetContentObservesLaterChangesToTheFile)
{
    const auto target = path("changing.txt");
    writeFile(target, "first");
    const FileUtil file(target);
    EXPECT_EQ(file.getContent(), "first");

    writeFile(target, "a different and longer value");
    EXPECT_EQ(file.getContent(), "a different and longer value");
    writeFile(target, "");
    EXPECT_TRUE(file.getContent().empty());
}

TEST_F(FileUtilTests, GetPosLenReadsPrefixMiddleSuffixAndWholeFile)
{
    writeFile(path("range.txt"), "abcdef");
    const FileUtil file(path("range.txt"));

    EXPECT_EQ(file.getPosLen(0, 2), "ab");
    EXPECT_EQ(file.getPosLen(2, 3), "cde");
    EXPECT_EQ(file.getPosLen(4, 2), "ef");
    EXPECT_EQ(file.getPosLen(5, 1), "f");
    EXPECT_EQ(file.getPosLen(0, 6), "abcdef");
}

TEST_F(FileUtilTests, GetPosLenCountsBytesAcrossNulsAndHighBytes)
{
    const std::string content("a\0b\x80\xff\r\n", 7);
    writeFile(path("range.dat"), content);

    EXPECT_EQ(FileUtil(path("range.dat")).getPosLen(1, 4), std::string("\0b\x80\xff", 4));
}

TEST_F(FileUtilTests, ZeroLengthReadsAreAllowedUpToTheEndOfTheFile)
{
    writeFile(path("range.txt"), "abcdef");
    const FileUtil file(path("range.txt"));

    EXPECT_TRUE(file.getPosLen(0, 0).empty());
    EXPECT_TRUE(file.getPosLen(3, 0).empty());
    EXPECT_TRUE(file.getPosLen(6, 0).empty());
    EXPECT_THROW(file.getPosLen(7, 0), std::out_of_range);
}

TEST_F(FileUtilTests, GetPosLenRejectsRangesPastTheEndAndOverflowingArguments)
{
    writeFile(path("range.txt"), "abcdef");
    const FileUtil file(path("range.txt"));

    EXPECT_THROW(file.getPosLen(0, 7), std::out_of_range);
    EXPECT_THROW(file.getPosLen(5, 2), std::out_of_range);
    EXPECT_THROW(file.getPosLen(6, 1), std::out_of_range);
    EXPECT_THROW(file.getPosLen(std::numeric_limits<uintmax_t>::max(), 1), std::out_of_range);
    EXPECT_THROW(file.getPosLen(1, std::numeric_limits<size_t>::max()), std::out_of_range);
    EXPECT_THROW(file.getPosLen(std::numeric_limits<uintmax_t>::max(), std::numeric_limits<size_t>::max()), std::out_of_range);
}

TEST_F(FileUtilTests, ReadsDoNotKeepThePreviousStreamPosition)
{
    writeFile(path("repeat.txt"), "abcdef");
    const FileUtil file(path("repeat.txt"));

    EXPECT_EQ(file.getPosLen(4, 2), "ef");
    EXPECT_EQ(file.getContent(), "abcdef");
    EXPECT_EQ(file.getContent(), "abcdef");
    EXPECT_THROW(file.getPosLen(10, 1), std::out_of_range);
    EXPECT_EQ(file.getPosLen(0, 2), "ab");
}

TEST_F(FileUtilTests, ReadsRejectMissingFilesAndDirectories)
{
    const FileUtil missing(path("missing.txt"));
    EXPECT_THROW(missing.getContent(), fs::filesystem_error);
    EXPECT_THROW(missing.getPosLen(0, 0), fs::filesystem_error);
    EXPECT_THROW(FileUtil(m_directory).getContent(), fs::filesystem_error);
}

TEST_F(FileUtilTests, SetContentCreatesAFileAndPreservesBinaryData)
{
    const std::string content = std::string("a\0b\r\n\x80\xff", 7) + u8"报告";
    const auto target = path(u8"新 文件.dat");

    FileUtil(target).setContent(content);

    EXPECT_TRUE(fs::is_regular_file(target));
    EXPECT_EQ(readFile(target), content);
}

TEST_F(FileUtilTests, SetContentTruncatesThePreviousLongerContent)
{
    writeFile(path("overwrite.txt"), "this is a much longer previous value");

    FileUtil(path("overwrite.txt")).setContent("new");

    EXPECT_EQ(readFile(path("overwrite.txt")), "new");
    EXPECT_EQ(fs::file_size(path("overwrite.txt")), 3U);
}

TEST_F(FileUtilTests, EmptyContentCreatesAnEmptyFileAndClearsAnExistingFile)
{
    FileUtil(path("new.txt")).setContent(std::string_view{});
    EXPECT_TRUE(fs::is_regular_file(path("new.txt")));
    EXPECT_EQ(fs::file_size(path("new.txt")), 0U);

    writeFile(path("existing.txt"), "previous data");
    FileUtil(path("existing.txt")).setContent("");
    EXPECT_TRUE(readFile(path("existing.txt")).empty());
}

TEST_F(FileUtilTests, SetContentWritesOnlyTheSelectedStringView)
{
    const std::string backing("prefixa\0bSUFFIX", 15);
    const auto content = std::string_view(backing).substr(6, 3);

    FileUtil(path("view.dat")).setContent(content);

    EXPECT_EQ(readFile(path("view.dat")), std::string("a\0b", 3));
}

TEST_F(FileUtilTests, SetContentRejectsDirectoriesAndMissingParentDirectories)
{
    EXPECT_THROW(FileUtil(m_directory).setContent("data"), std::ios_base::failure);
    EXPECT_THROW(FileUtil(path("missing/child.txt")).setContent("data"), std::ios_base::failure);
    EXPECT_FALSE(fs::exists(path("missing")));
}

TEST_F(FileUtilTests, ReadPermissionFailuresAreReportedAsStreamErrors)
{
    const auto target = path("unreadable.txt");
    writeFile(target, "data");
    const ScopedPermissions permissions(target, fs::perms::owner_write);
    std::ifstream probe(target, std::ios::binary);
    if (probe.is_open()) GTEST_SKIP() << "The current user can read files without read permission";

    EXPECT_THROW(FileUtil(target).getContent(), std::ios_base::failure);
}

TEST_F(FileUtilTests, WritePermissionFailuresPreserveTheExistingContent)
{
    const auto target = path("readonly.txt");
    writeFile(target, "keep this data");
    const ScopedPermissions permissions(target, fs::perms::owner_read);
    std::ofstream probe(target, std::ios::binary | std::ios::app);
    if (probe.is_open()) GTEST_SKIP() << "The current user can write files without write permission";

    EXPECT_THROW(FileUtil(target).setContent("replacement"), std::ios_base::failure);
    EXPECT_EQ(readFile(target), "keep this data");
}

TEST(FileUtilWriteFailureTests, SetContentReportsBufferedWriteFailure)
{
    // Linux 的 /dev/full 可以打开，但写出时报告空间不足，用它检查显式 close() 的错误传播。
    const fs::path device("/dev/full");
    if (!fs::is_character_file(device)) GTEST_SKIP() << "/dev/full is unavailable";
    std::ofstream probe(device, std::ios::binary);
    if (!probe.is_open()) GTEST_SKIP() << "/dev/full cannot be opened for writing";
    probe.close();

    EXPECT_THROW(FileUtil(device).setContent("buffered data"), std::ios_base::failure);
}

TEST_F(FileUtilTests, ExistsDistinguishesMissingFilesRegularFilesAndDirectories)
{
    writeFile(path("file.txt"), "data");
    fs::create_directory(path("directory"));

    EXPECT_FALSE(FileUtil(path("missing.txt")).exists());
    EXPECT_TRUE(FileUtil(path("file.txt")).exists());
    EXPECT_TRUE(FileUtil(path("directory")).exists());
}

TEST_F(FileUtilTests, ExistsFollowsSymbolicLinkTargets)
{
    if (!canCreateSymlinks()) GTEST_SKIP() << "Symbolic links are unavailable";

    writeFile(path("file.txt"), "data");
    fs::create_directory(path("directory"));
    fs::create_symlink("file.txt", path("file-link"));
    fs::create_directory_symlink("directory", path("directory-link"));
    fs::create_symlink("missing.txt", path("broken-link"));

    EXPECT_TRUE(FileUtil(path("file-link")).exists());
    EXPECT_TRUE(FileUtil(path("directory-link")).exists());
    EXPECT_FALSE(FileUtil(path("broken-link")).exists());
}

TEST_F(FileUtilTests, FileAccessFollowsTheLinkButFileNameKeepsTheLinkName)
{
    if (!canCreateSymlinks()) GTEST_SKIP() << "Symbolic links are unavailable";

    const auto target = path("target.txt");
    writeFile(target, "original");
    fs::create_symlink("target.txt", path("file-link"));
    const FileUtil link(path("file-link"));

    EXPECT_EQ(link.fileName(), "file-link");
    EXPECT_EQ(link.fileSize(), 8U);
    EXPECT_EQ(link.getContent(), "original");
    EXPECT_EQ(link.lastModifyTime(), fs::last_write_time(target));

    link.setContent("new");
    EXPECT_TRUE(fs::is_symlink(path("file-link")));
    EXPECT_EQ(readFile(target), "new");
}

TEST_F(FileUtilTests, CreateDirectoryCreatesAllMissingParents)
{
    const auto target = path(u8"父目录/child/leaf");

    FileUtil(target).createDirectory();

    EXPECT_TRUE(fs::is_directory(target));
    EXPECT_TRUE(fs::is_directory(path(u8"父目录")));
}

TEST_F(FileUtilTests, CreateDirectoryAcceptsAnExistingDirectory)
{
    fs::create_directory(path("existing"));
    writeFile(path("existing/keep.txt"), "keep");

    EXPECT_NO_THROW(FileUtil(path("existing")).createDirectory());
    EXPECT_EQ(readFile(path("existing/keep.txt")), "keep");
}

TEST_F(FileUtilTests, CreateDirectoryRejectsFileConflicts)
{
    writeFile(path("file.txt"), "keep");

    EXPECT_THROW(FileUtil(path("file.txt")).createDirectory(), fs::filesystem_error);
    EXPECT_THROW(FileUtil(path("file.txt/child")).createDirectory(), fs::filesystem_error);
    EXPECT_EQ(readFile(path("file.txt")), "keep");
}

TEST_F(FileUtilTests, DirectoryPermissionFailuresAreReported)
{
    const auto parent = path("restricted");
    fs::create_directory(parent);
    const ScopedPermissions permissions(parent, fs::perms::owner_read | fs::perms::owner_exec);
    std::error_code error;
    if (fs::create_directory(parent / "probe", error)) GTEST_SKIP() << "The current user can create files in a directory without write permission";
    ASSERT_TRUE(error);

    EXPECT_THROW(FileUtil(parent / "child").createDirectory(), fs::filesystem_error);
}

TEST_F(FileUtilTests, ScanDirectoryReturnsNoEntriesForAnEmptyDirectory)
{
    EXPECT_TRUE(FileUtil(m_directory).scanDirectory().empty());
}

TEST_F(FileUtilTests, ScanDirectoryReturnsOnlyDirectRegularFiles)
{
    writeFile(path("report.txt"), "data");
    writeFile(path(".hidden"), "");
    writeFile(path(u8"报告 文件.txt"), "data");
    fs::create_directories(path("child/nested"));
    writeFile(path("child/inside.txt"), "data");

    const auto files = FileUtil(m_directory).scanDirectory();
    const std::set<fs::path> expected = {path("report.txt"), path(".hidden"), path(u8"报告 文件.txt")};

    // 目录遍历顺序没有保证，比较路径集合，同时检查没有重复或额外条目。
    EXPECT_EQ(files.size(), expected.size());
    EXPECT_EQ((std::set<fs::path>(files.begin(), files.end())), expected);
}

TEST_F(FileUtilTests, ScanDirectoryKeepsTheRelativePathPrefix)
{
    writeFile(path("report.txt"), "data");
    const auto relative = fs::relative(m_directory, fs::current_path());
    if (relative.empty()) GTEST_SKIP() << "The temporary directory and working directory have different roots";
    ASSERT_TRUE(relative.is_relative());

    const auto files = FileUtil(relative).scanDirectory();

    ASSERT_EQ(files.size(), 1U);
    EXPECT_EQ(files.front(), relative / "report.txt");
}

TEST_F(FileUtilTests, ScanDirectoryFollowsFileLinksAndSkipsDirectoryAndBrokenLinks)
{
    if (!canCreateSymlinks()) GTEST_SKIP() << "Symbolic links are unavailable";

    writeFile(path("target.txt"), "data");
    fs::create_directory(path("child"));
    fs::create_symlink("target.txt", path("file-link"));
    fs::create_directory_symlink("child", path("directory-link"));
    fs::create_symlink("missing.txt", path("broken-link"));

    const auto files = FileUtil(m_directory).scanDirectory();
    const std::set<fs::path> expected = {path("target.txt"), path("file-link")};

    EXPECT_EQ(files.size(), expected.size());
    EXPECT_EQ((std::set<fs::path>(files.begin(), files.end())), expected);
}

TEST_F(FileUtilTests, ScanDirectorySkipsLinksToSpecialFiles)
{
    if (!fs::is_character_file("/dev/null")) GTEST_SKIP() << "/dev/null is unavailable";
    fs::create_symlink("/dev/null", path("device-link"));

    EXPECT_TRUE(FileUtil(m_directory).scanDirectory().empty());
}

TEST_F(FileUtilTests, ScanDirectoryRejectsMissingPathsAndRegularFiles)
{
    writeFile(path("file.txt"), "data");

    EXPECT_THROW(FileUtil(path("missing")).scanDirectory(), fs::filesystem_error);
    EXPECT_THROW(FileUtil(path("file.txt")).scanDirectory(), fs::filesystem_error);
}

TEST_F(FileUtilTests, ScanAndExistsReportDirectoryAccessErrors)
{
    const auto parent = path("restricted");
    fs::create_directory(parent);
    writeFile(parent / "file.txt", "data");
    const ScopedPermissions permissions(parent, fs::perms::none);
    std::error_code error;
    const fs::directory_iterator probe(parent, error);
    if (!error) GTEST_SKIP() << "The current user can access directories without permissions";

    EXPECT_THROW(FileUtil(parent).scanDirectory(), fs::filesystem_error);
    EXPECT_THROW(FileUtil(parent / "file.txt").exists(), fs::filesystem_error);
}

// 内存内容的压缩与解压。

TEST(FileUtilCompressionTests, EmptyInputProducesEmptyOutput)
{
    for (int format : {bundle::RAW, bundle::LZIP, bundle::MINIZ})
    {
        SCOPED_TRACE(format);
        EXPECT_TRUE(FileUtil::Compress(std::string_view{}, format).empty());
        EXPECT_TRUE(FileUtil::Compress("", format).empty());
    }
    EXPECT_TRUE(FileUtil::UnCompress(std::string_view{}).empty());
    EXPECT_TRUE(FileUtil::UnCompress("").empty());
}

TEST(FileUtilCompressionTests, CompressProducesBundleCompatibleDataInTheRequestedFormat)
{
    const auto input = compressionPayload();

    for (int format : {bundle::LZIP, bundle::MINIZ})
    {
        SCOPED_TRACE(format);
        const auto packed = FileUtil::Compress(input, format);

        ASSERT_TRUE(bundle::is_packed(packed));
        // 与指定算法的库输出比较，确认 format 没有被忽略。
        EXPECT_EQ(packed, bundle::pack(format, input));
        EXPECT_LT(packed.size(), input.size());
        // 用库本身解码，避免只验证两个待测接口之间的往返。
        EXPECT_EQ(bundle::unpack(packed), input);
    }
}

TEST(FileUtilCompressionTests, UnCompressReadsDataProducedDirectlyByBundle)
{
    const auto input = compressionPayload();

    for (int format : {bundle::LZIP, bundle::MINIZ})
    {
        SCOPED_TRACE(format);
        const auto packed = bundle::pack(format, input);
        ASSERT_TRUE(bundle::is_packed(packed));

        EXPECT_EQ(FileUtil::UnCompress(packed), input);
    }
}

TEST(FileUtilCompressionTests, RawFormatPreservesBinaryContent)
{
    std::string input;
    for (unsigned int byte = 0; byte < 256; ++byte) input.push_back(static_cast<char>(byte));

    const auto packed = FileUtil::Compress(input, bundle::RAW);

    EXPECT_EQ(packed, input);
    EXPECT_EQ(FileUtil::UnCompress(packed), input);
}

TEST(FileUtilCompressionTests, SmallUnhelpfulInputIsPreservedByBundle)
{
    const std::string input = "abc";

    EXPECT_EQ(FileUtil::Compress(input, bundle::LZIP), input);
    EXPECT_EQ(FileUtil::UnCompress(input), input);
}

TEST(FileUtilCompressionTests, BinaryDataWithEveryByteValueRoundTrips)
{
    std::string input;
    for (int repetition = 0; repetition < 64; ++repetition)
        for (unsigned int byte = 0; byte < 256; ++byte) input.push_back(static_cast<char>(byte));

    const auto packed = FileUtil::Compress(input, bundle::LZIP);

    EXPECT_EQ(FileUtil::UnCompress(packed), input);
}

TEST(FileUtilCompressionTests, RoundTripsAcrossSmallInputSizes)
{
    const size_t sizes[] = {1, 2, 7, 15, 16, 31, 32, 33, 255, 256, 257, 4096};
    for (size_t size : sizes)
    {
        SCOPED_TRACE(size);
        const std::string input(size, 'x');
        EXPECT_EQ(FileUtil::UnCompress(FileUtil::Compress(input, bundle::LZIP)), input);
    }
}

TEST(FileUtilCompressionTests, CompressReadsOnlyTheSelectedStringView)
{
    const auto input = compressionPayload();
    const auto backing = "PREFIX" + input + "SUFFIX";
    const auto view = std::string_view(backing).substr(6, input.size());

    EXPECT_EQ(bundle::unpack(FileUtil::Compress(view, bundle::LZIP)), input);
}

TEST(FileUtilCompressionTests, UnCompressAcceptsAStringViewInsideALargerBuffer)
{
    const auto input = compressionPayload();
    const auto packed = bundle::pack(bundle::LZIP, input);
    ASSERT_TRUE(bundle::is_packed(packed));
    const auto backing = "PREFIX" + packed + "SUFFIX";
    const auto view = std::string_view(backing).substr(6, packed.size());

    EXPECT_EQ(FileUtil::UnCompress(view), input);
}

TEST(FileUtilCompressionTests, RejectsInvalidFormatsEvenForEmptyContent)
{
    const int invalid[] = {-1, std::numeric_limits<int>::min(), bundle::BZIP2 + 1, std::numeric_limits<int>::max()};
    for (int format : invalid)
    {
        SCOPED_TRACE(format);
        EXPECT_THROW(FileUtil::Compress("data", format), std::invalid_argument);
        EXPECT_THROW(FileUtil::Compress(std::string_view{}, format), std::invalid_argument);
    }
}
