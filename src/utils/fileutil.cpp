#include "utils/fileutil.h"

#include <fstream>
#include <limits>
#include <stdexcept>

#include <bundle.h>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(utils)


std::string FileUtil::getPosLen(uintmax_t pos, size_t len) const
{
    auto size = fileSize();

    // 第二个判断条件，用减法控制范围，避免 pos + len 在数值很大时溢出。
    if (pos > size || len > size - pos) throw std::out_of_range("FileUtil::getPosLen(): requested range exceeds file size");

    // 文件流的位置和读取长度使用有符号类型，确认能容纳后再转换。
    if (pos > static_cast<uintmax_t>(std::numeric_limits<std::streamoff>::max()) || len > static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max())) throw std::length_error("FileUtil::getPosLen(): requested range exceeds stream limits");

    // ifstream 是文件输入流，open() 接收文件路径，之后从该文件读取内容。中途抛出异常时，已打开的文件仍会在析构时自动关闭。
    std::ifstream file;

    // failbit：操作失败，例如文件打开失败、定位失败，或者没有读满要求的字节数。
    // badbit：文件流出现严重错误，例如底层读取出错。
    file.exceptions(std::ios::failbit | std::ios::badbit);

    file.open(m_path, std::ios::binary);

    // 把偏移位置移动到以 __dir 为起点，偏移量为 __off 的位置。
    // 第一个参数 __off：偏移量是多少。
    // 第二个参数 __dir：相对哪里偏移。std::ios::beg 文件开头；std::ios::cur 当前读取位置；std::ios::end 文件末尾。
    file.seekg(static_cast<std::streamoff>(pos), std::ios::beg);

    // 先分配 len 个字节，再直接读入字符串，避免把二进制内容当作以 '\0' 结尾的文本。
    std::string content(len, '\0');
    // std::ifstream 的 read()：从文件当前读取位置开始，读取 len 个字节，写入 content。
    if (0 != len) file.read(content.data(), static_cast<std::streamsize>(len));

    file.close();


    return content;
}

void FileUtil::setContent(std::string_view content) const
{
    // 先检查长度，再打开文件，避免参数不合法时就清空已有内容。
    if (content.size() > static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max())) throw std::length_error("FileUtil::setContent(): content exceeds stream limits");

    std::ofstream file;
    file.exceptions(std::ios::failbit | std::ios::badbit);

    // binary 按原始字节写入；trunc 清空已有内容，保证写入较短内容时不会留下旧文件的尾部。
    file.open(m_path, std::ios::binary | std::ios::trunc);

    // 按明确长度写入，不会在遇到 '\0' 时提前停止；空内容只需清空文件。
    if (!content.empty()) file.write(content.data(), static_cast<std::streamsize>(content.size()));

    // close() 会写出文件流缓冲区中剩余的数据并关闭文件，析构自动关闭时也会尝试做这些事。
    // 这里显式调用，是为了在函数返回前检查最后的写出和关闭是否失败。写出的语义是把数据交给操作系统，操作系统可能先缓存再落盘；close() 成功不保证已落盘。若需要等待持久化完成，还要配合如 Linux 下 fsync() 等同步接口。
    file.close();
}

std::vector<std::filesystem::path> FileUtil::scanDirectory() const
{
    std::vector<std::filesystem::path> res;

    // directory_iterator 表示只遍历这一层；is_regular_file() 排除目录和其他特殊文件。
    for (auto &entry : std::filesystem::directory_iterator(m_path))
    {
        if (entry.is_regular_file()) res.push_back(entry.path());
    }

    return res;
}

std::string FileUtil::Compress(std::string_view content, int format)
{
    if (format < bundle::RAW || format > bundle::BZIP2) throw std::invalid_argument("FileUtil::Compress(): invalid bundle format");

    std::string packed;
    bundle::pack(static_cast<unsigned int>(format), packed, content);


    return packed;
}

std::string FileUtil::UnCompress(std::string_view content)
{
    if (content.empty()) return {};

    std::string unpacked;
    bundle::unpack(unpacked, content);


    return unpacked;
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
