#include "server/utils.h"

#include <fstream>
#include <limits>
#include <stdexcept>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


unsigned char toHex(unsigned char x)
{
    if (x > 15) throw std::invalid_argument("toHex(): hex value must be between 0 and 15");

    // "0123456789ABCDEF" 的类型是 const char[17]（含末尾 '\0'），可以直接用 [x] 取字符。例如 x 为 10 时取出 'A'。
    return static_cast<unsigned char>("0123456789ABCDEF"[x]);
}

unsigned char fromHex(unsigned char x)
{
    if (x >= '0' && x <= '9')
    {
        return static_cast<unsigned char>(x - '0');
    }
    else if (x >= 'A' && x <= 'F')
    {
        return static_cast<unsigned char>(x - 'A' + 10);
    }
    else if (x >= 'a' && x <= 'f')
    {
        return static_cast<unsigned char>(x - 'a' + 10);
    }
    else
    {
        throw std::invalid_argument("fromHex(): invalid hexadecimal character");
    }
}

std::string urlDecode(const std::string &str)
{
    std::string decoded;
    decoded.reserve(str.size());

    for (int i = 0; i < str.size(); ++i)
    {
        if ('%' != str[i])
        {
            // 普通内容直接放入结果；路径中的 '+' 就是加号，不转换为空格。
            decoded.push_back(str[i]);
            continue;
        }

        // 遇到 '%' 后，需要再读取两位十六进制数字。
        if (str.size() - i < 3) throw std::invalid_argument("urlDecode(): incomplete URL percent escape");

        // 先把这两位字符转换为数值，再按“高位 * 16 + 低位”合成一个字节。
        unsigned char high = fromHex(static_cast<unsigned char>(str[1 + i]));
        unsigned char low = fromHex(static_cast<unsigned char>(str[2 + i]));
        // 例如 "%20" 得到 2 * 16 + 0 = 32，将对应的空格放入结果。
        decoded.push_back(static_cast<char>(16 * high + low));

        // 后两位已经处理完，跳过它们，下一轮继续读取后面的内容。
        i += 2;
    }


    return decoded;
}

std::string FileUtil::getPosLen(uintmax_t pos, size_t len) const
{
    auto size = fileSize();

    // 第二个判断条件，用减法控制范围，避免 pos + len 在数值很大时溢出。
    if (pos > size || len > size - pos) throw std::out_of_range("FileUtil::getPosLen(): requested range exceeds file size");

    // 文件流的位置和读取长度使用有符号类型，确认能容纳后再转换。
    if (pos > static_cast<uintmax_t>(std::numeric_limits<std::streamoff>::max()) || len > static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max())) throw std::length_error("FileUtil::getPosLen(): requested range exceeds stream limits");

    // 文件输入流，打开的文件由析构函数关闭。
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


    return content;
}

void FileUtil::setContent(std::string_view content) const
{
}

void FileUtil::compress(std::string_view content, int format) const
{
}

void FileUtil::unCompress(const std::filesystem::path &downloadPath) const
{
}

bool FileUtil::exists() const
{
}

void FileUtil::createDirectory() const
{
}

std::vector<std::filesystem::path> FileUtil::scanDirectory() const
{
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
