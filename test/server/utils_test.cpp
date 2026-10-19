#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <bundle.h>

#include "utils/common.h"
#include "utils/fileutil.h"
#include "utils/jsonutil.h"


namespace
{

    using utils::FileUtil;
    using utils::JsonUtil;

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

        fs::path path(const fs::path &name) const { return m_directory / name; }

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


// 十六进制与 URL 解码。

TEST(ServerUtilsTests, ToHexProducesUppercaseDigits)
{
    const std::string digits = "0123456789ABCDEF";

    for (unsigned char value = 0; value < 16; ++value)
    {
        EXPECT_EQ(utils::toHex(value), static_cast<unsigned char>(digits[value]));
    }
}

TEST(ServerUtilsTests, ToHexRejectsValuesOutsideOneNibble)
{
    for (unsigned int value = 16; value < 256; ++value)
    {
        SCOPED_TRACE(value);
        EXPECT_THROW(utils::toHex(static_cast<unsigned char>(value)), std::invalid_argument);
    }
}

TEST(ServerUtilsTests, FromHexAcceptsBothLetterCases)
{
    const std::string uppercase = "0123456789ABCDEF";
    const std::string lowercase = "0123456789abcdef";

    for (unsigned char value = 0; value < 16; ++value)
    {
        EXPECT_EQ(utils::fromHex(static_cast<unsigned char>(uppercase[value])), value);
        EXPECT_EQ(utils::fromHex(static_cast<unsigned char>(lowercase[value])), value);
    }
}

TEST(ServerUtilsTests, FromHexRejectsNonHexadecimalCharacters)
{
    const std::string valid = "0123456789ABCDEFabcdef";
    for (unsigned int value = 0; value < 256; ++value)
    {
        const auto character = static_cast<unsigned char>(value);
        if (valid.find(static_cast<char>(character)) != std::string::npos) continue;

        SCOPED_TRACE(value);
        EXPECT_THROW(utils::fromHex(character), std::invalid_argument);
    }
}

TEST(ServerUtilsTests, UrlDecodePreservesUnescapedText)
{
    const std::string inputs[] = {"", "file.txt", "/download/a+b.txt", u8"云存储.txt"};

    for (const auto &input : inputs)
    {
        SCOPED_TRACE(input);
        EXPECT_EQ(utils::urlDecode(input), input);
    }
}

TEST(ServerUtilsTests, UrlDecodeHandlesUtf8AndMixedCaseEscapes)
{
    EXPECT_EQ(utils::urlDecode("%e4%BA%91%E5%ad%98%e5%82%A8%20a%2Bb.txt"),
              u8"云存储 a+b.txt");
    EXPECT_EQ(utils::urlDecode("%2f%2F%3a"), "//:");
}

TEST(ServerUtilsTests, UrlDecodePreservesPlusAndDecodesOnlyOnce)
{
    EXPECT_EQ(utils::urlDecode("a+b%2Bc%20d"), "a+b+c d");
    EXPECT_EQ(utils::urlDecode("%25"), "%");
    EXPECT_EQ(utils::urlDecode("%252F"), "%2F");
    EXPECT_EQ(utils::urlDecode("%25%32%46"), "%2F");
}

TEST(ServerUtilsTests, UrlDecodePreservesNulsAndHighBytes)
{
    const std::string bytes("\0\x01\x7f\x80\xff", 5);

    EXPECT_EQ(utils::urlDecode("%00%01%7F%80%FF"), bytes);
    EXPECT_EQ(utils::urlDecode(bytes), bytes);
    EXPECT_EQ(utils::urlDecode(std::string("a\0%20b", 6)), std::string("a\0 b", 4));
}

TEST(ServerUtilsTests, UrlDecodeHandlesEveryEscapedByte)
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

    EXPECT_EQ(utils::urlDecode(encoded), expected);
}

TEST(ServerUtilsTests, UrlDecodeRejectsMalformedEscapes)
{
    const std::string inputs[] = {
        "%", "%0", "file%", "file%A", "%GG", "%G0", "%0g", "%1Z",
        "%Z1", "% 0", "%%20", "valid%20then%", std::string("%\0A", 3)};

    for (const auto &input : inputs)
    {
        SCOPED_TRACE(input);
        EXPECT_THROW(utils::urlDecode(input), std::invalid_argument);
    }
}

TEST(ServerUtilsTests, UrlDecodeRejectsEveryInvalidHexByteInEitherPosition)
{
    const std::string valid = "0123456789ABCDEFabcdef";
    for (unsigned int value = 0; value < 256; ++value)
    {
        const auto character = static_cast<char>(value);
        if (valid.find(character) != std::string::npos) continue;

        SCOPED_TRACE(value);
        EXPECT_THROW(utils::urlDecode(std::string("%") + character + '0'), std::invalid_argument);
        EXPECT_THROW(utils::urlDecode(std::string("%0") + character), std::invalid_argument);
    }
}

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
        {"", ""}, {"report.txt", "report.txt"}, {"data/report.tar.gz", "report.tar.gz"}, {"/missing/directory/report.txt", "report.txt"}, {"data/", ""}, {"/", ""}, {".", "."}, {"..", ".."}, {u8"不存在/云 存储.txt", u8"云 存储.txt"}};

    for (const auto &item : cases)
    {
        SCOPED_TRACE(item.input.string());
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
    ASSERT_TRUE(relative.is_relative());

    const auto files = FileUtil(relative).scanDirectory();

    ASSERT_EQ(files.size(), 1U);
    EXPECT_EQ(files.front(), relative / "report.txt");
}

TEST_F(FileUtilTests, ScanDirectoryFollowsFileLinksAndSkipsDirectoryAndBrokenLinks)
{
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

// JSON 值与文本转换。

TEST(JsonUtilTests, SerializeProducesJsonReadableByJsonCpp)
{
    Json::Value value;
    value["name"] = "report.txt";
    value["size"] = Json::UInt64(4096);
    value["ready"] = true;
    value["metadata"]["owner"] = "alice";
    value["tags"].append("documents");
    value["tags"].append("backup");

    const auto document = JsonUtil::Serialize(value);
    Json::CharReaderBuilder builder;
    std::istringstream stream(document);
    Json::Value restored;
    std::string errors;

    ASSERT_TRUE(Json::parseFromStream(builder, stream, &restored, &errors)) << errors;
    ASSERT_TRUE(restored.isObject());
    EXPECT_EQ(restored.size(), value.size());
    EXPECT_EQ(restored["name"].asString(), "report.txt");
    // JSON 文本不区分有符号和无符号整数，读回后按数值比较。
    EXPECT_EQ(restored["size"].asUInt64(), 4096U);
    EXPECT_TRUE(restored["ready"].asBool());
    EXPECT_EQ(restored["metadata"], value["metadata"]);
    EXPECT_EQ(restored["tags"], value["tags"]);
}

TEST(JsonUtilTests, SerializeEmitsUtf8KeysAndValues)
{
    Json::Value value;
    value[u8"文件名"] = u8"云存储/报告📦.txt";

    const auto document = JsonUtil::Serialize(value);

    EXPECT_NE(document.find(u8"文件名"), std::string::npos);
    EXPECT_NE(document.find(u8"云存储/报告📦.txt"), std::string::npos);
    EXPECT_EQ(JsonUtil::UnSerialize(document), value);
}

TEST(JsonUtilTests, SerializeEscapesControlCharactersAndPreservesTheirValues)
{
    Json::Value value;
    const std::string text("a\0b\n\r\t\"\\", 8);
    value["text"] = text;

    const auto document = JsonUtil::Serialize(value);

    EXPECT_EQ(document.find('\0'), std::string::npos);
    for (const auto *escape : {"\\u0000", "\\n", "\\r", "\\t", "\\\"", "\\\\"})
    {
        SCOPED_TRACE(escape);
        EXPECT_NE(document.find(escape), std::string::npos);
    }
    EXPECT_EQ(JsonUtil::UnSerialize(document)["text"].asString(), text);
}

TEST(JsonUtilTests, SerializeHandlesScalarRootsAndEmptyContainers)
{
    const struct
    {
        Json::Value value;
        std::string expected;
    } cases[] = {
        {Json::Value(), "null"}, {Json::Value(true), "true"}, {Json::Value(false), "false"}, {Json::Value(-42), "-42"}, {Json::Value(1.25), "1.25"}, {Json::Value("text"), R"("text")"}, {Json::Value(Json::arrayValue), "[]"}, {Json::Value(Json::objectValue), "{}"}};

    for (const auto &item : cases)
    {
        SCOPED_TRACE(item.expected);
        EXPECT_EQ(JsonUtil::Serialize(item.value), item.expected);
        EXPECT_EQ(JsonUtil::UnSerialize(item.expected), item.value);
    }
}

TEST(JsonUtilTests, UnSerializeReadsNestedStorageMetadata)
{
    const auto value = JsonUtil::UnSerialize(R"({
        "files": [{"name":"report.txt","size":4096}, {"name":"empty.txt","size":0}],
        "compressed": true,
        "ratio": 0.75,
        "checksum": null,
        "metadata": {"owner":"alice","version":2}
    })");

    ASSERT_TRUE(value.isObject());
    ASSERT_TRUE(value["files"].isArray());
    ASSERT_EQ(value["files"].size(), 2U);
    EXPECT_EQ(value["files"][0]["name"].asString(), "report.txt");
    EXPECT_EQ(value["files"][0]["size"].asUInt64(), 4096U);
    EXPECT_EQ(value["files"][1]["size"].asUInt64(), 0U);
    EXPECT_TRUE(value["compressed"].asBool());
    EXPECT_DOUBLE_EQ(value["ratio"].asDouble(), 0.75);
    EXPECT_TRUE(value["checksum"].isNull());
    EXPECT_EQ(value["metadata"]["owner"].asString(), "alice");
    EXPECT_EQ(value["metadata"]["version"].asInt(), 2);
}

TEST(JsonUtilTests, UnSerializeAcceptsWhitespaceAroundTheDocument)
{
    EXPECT_TRUE(JsonUtil::UnSerialize(" \t\r\n {} \n ").isObject());
    EXPECT_TRUE(JsonUtil::UnSerialize(" \n [] \t ").isArray());
    EXPECT_TRUE(JsonUtil::UnSerialize(" \n null \t ").isNull());
    EXPECT_EQ(JsonUtil::UnSerialize(" \n 42 \t ").asInt(), 42);
}

TEST(JsonUtilTests, UnSerializeDecodesUnicodeEscapesAndSurrogatePairs)
{
    const auto value = JsonUtil::UnSerialize(R"({"text":"\u4e91\u5b58\u50a8\ud83d\udce6"})");

    EXPECT_EQ(value["text"].asString(), u8"云存储📦");
}

TEST(JsonUtilTests, UnSerializeDecodesEscapedNulsAndOtherControlCharacters)
{
    const auto value = JsonUtil::UnSerialize(R"({"text":"\u0000\"\\\/\b\f\n\r\t"})");

    EXPECT_EQ(value["text"].asString(), std::string("\0\"\\/\b\f\n\r\t", 9));
}

TEST(JsonUtilTests, Preserves64BitIntegerBoundariesAndDoublePrecision)
{
    const auto value = JsonUtil::UnSerialize("[-9223372036854775808,9223372036854775807,18446744073709551615,1.2345678901234567]");

    ASSERT_EQ(value.size(), 4U);
    EXPECT_EQ(value[0].asInt64(), std::numeric_limits<Json::Int64>::min());
    EXPECT_EQ(value[1].asInt64(), std::numeric_limits<Json::Int64>::max());
    EXPECT_EQ(value[2].asUInt64(), std::numeric_limits<Json::UInt64>::max());
    EXPECT_DOUBLE_EQ(value[3].asDouble(), 1.2345678901234567);
    EXPECT_EQ(JsonUtil::UnSerialize(JsonUtil::Serialize(value)), value);
}

TEST(JsonUtilTests, UnSerializeReadsOnlyTheSelectedStringView)
{
    const std::string backing = R"(PREFIX{"id":7,"name":"report.txt"}SUFFIX)";
    const auto document = std::string_view(backing).substr(6, backing.size() - 12);

    const auto value = JsonUtil::UnSerialize(document);

    EXPECT_EQ(value["id"].asInt(), 7);
    EXPECT_EQ(value["name"].asString(), "report.txt");
}

TEST(JsonUtilTests, UnSerializeDoesNotReadPastTheEndOfATruncatedView)
{
    const std::string complete = R"({"ok":true})";
    const auto truncated = std::string_view(complete).substr(0, complete.size() - 1);

    // 底层字符串是合法 JSON，但传入的视图缺少结尾的 '}'，不能偷偷读取视图之外的字节。
    EXPECT_THROW(JsonUtil::UnSerialize(truncated), std::invalid_argument);
}

TEST(JsonUtilTests, UnSerializeRejectsEmptyAndMalformedDocuments)
{
    const std::string_view invalid[] = {
        {}, "", " \t\n", "{", "[", R"({"key" 1})", R"({"key":})", R"({"key":1 "other":2})", "[1 2]", "{unquoted:1}", R"({"key":"unterminated})", R"({"key":"\q"})", R"({"key":"\u12xz"})", "[truee]", "[1e]", "[1.2.3]"};

    for (const auto document : invalid)
    {
        SCOPED_TRACE(std::string(document));
        EXPECT_THROW(JsonUtil::UnSerialize(document), std::invalid_argument);
    }
}

TEST(JsonUtilTests, ParseFailuresIncludeTheJsonCppDiagnostic)
{
    try
    {
        JsonUtil::UnSerialize("{\n  \"name\":\n}");
        FAIL() << "Expected a parsing error";
    }
    catch (const std::invalid_argument &error)
    {
        const std::string prefix = "JsonUtil::UnSerialize(): ";
        const std::string message = error.what();
        EXPECT_EQ(message.find(prefix), 0U);
        EXPECT_GT(message.size(), prefix.size());
    }
}

TEST(JsonUtilTests, UsesJsonCppDefaultCommentAndTrailingCommaRules)
{
    // 工具类沿用库的默认配置；第三方库测试中的严格模式没有应用到这个接口。
    const auto value = JsonUtil::UnSerialize(R"({"id":/* metadata */7,})");

    EXPECT_EQ(value["id"].asInt(), 7);
}

TEST(JsonUtilTests, AFailedParseDoesNotAffectLaterCalls)
{
    EXPECT_EQ(JsonUtil::UnSerialize(R"({"id":1})")["id"].asInt(), 1);
    EXPECT_THROW(JsonUtil::UnSerialize("{"), std::invalid_argument);
    EXPECT_EQ(JsonUtil::UnSerialize(R"({"id":2})")["id"].asInt(), 2);
}

TEST(JsonUtilTests, RoundTripsALargeStorageMetadataCollection)
{
    Json::Value metadata;
    metadata["files"] = Json::Value(Json::arrayValue);
    for (Json::ArrayIndex i = 0; i < 512; ++i)
    {
        Json::Value file;
        file["name"] = u8"文件-" + std::to_string(i) + ".txt";
        file["size"] = Json::UInt64(i) * 4096;
        file["compressed"] = i % 2 == 0;
        metadata["files"].append(file);
    }

    const auto restored = JsonUtil::UnSerialize(JsonUtil::Serialize(metadata));
    ASSERT_TRUE(restored["files"].isArray());
    ASSERT_EQ(restored["files"].size(), metadata["files"].size());
    for (Json::ArrayIndex i = 0; i < metadata["files"].size(); ++i)
    {
        SCOPED_TRACE(i);
        const auto &expected = metadata["files"][i];
        const auto &actual = restored["files"][i];
        EXPECT_EQ(actual["name"].asString(), expected["name"].asString());
        EXPECT_EQ(actual["size"].asUInt64(), expected["size"].asUInt64());
        EXPECT_EQ(actual["compressed"].asBool(), expected["compressed"].asBool());
    }
}
