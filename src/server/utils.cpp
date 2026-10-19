#include "server/utils.h"

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

// TODO
FileUtil::FileUtil(std::filesystem::path path) : m_path(std::move(path))
{
}

std::uintmax_t FileUtil::fileSize() const
{
}

std::filesystem::file_time_type FileUtil::lastAccessTime() const
{
}

std::filesystem::file_time_type FileUtil::lastModifyTime() const
{
}

std::filesystem::path FileUtil::fileName() const
{
}

std::string FileUtil::getPosLen(std::uintmax_t pos, size_t len) const
{
}

std::string FileUtil::getContent() const
{
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
