#include "utils/common.h"

#include <stdexcept>
#include <system_error>

#ifdef DLOG_CLOUD_STORAGE_OS_WIN32
#include <windows.h>
#endif


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(utils)


std::filesystem::path executableFullPath()
{
#ifdef DLOG_CLOUD_STORAGE_OS_WIN32

    // 使用宽字符支持中文路径，固定缓冲区覆盖 Windows 的长路径长度范围。
    constexpr const DWORD kMaxPathLength = 32768;
    wchar_t buffer[kMaxPathLength] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, kMaxPathLength);
    if (0 == length)
    {
        const DWORD error = GetLastError();
        throw std::system_error(static_cast<int>(error), std::system_category(), "executableFullPath(): GetModuleFileNameW() failed");
    }
    if (kMaxPathLength == length) throw std::system_error(ERROR_INSUFFICIENT_BUFFER, std::system_category(), "executableFullPath(): executable path was truncated");


    return std::filesystem::path(buffer, buffer + length);

#elif defined DLOG_CLOUD_STORAGE_OS_LINUX

    // /proc/self/exe 指向当前进程实际执行的文件，标准库负责读取完整的链接目标。
    return std::filesystem::read_symlink("/proc/self/exe");

#else

    throw std::system_error(std::make_error_code(std::errc::function_not_supported), "executableFullPath(): unsupported platform");

#endif
}

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

std::string percentEncode(const std::string &text, bool preserveSlash)
{
    std::string encoded;

    for (const unsigned char &ch : text)
    {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || '-' == ch || '_' == ch || '.' == ch || '~' == ch || (preserveSlash && '/' == ch))
        {
            encoded += static_cast<char>(ch);
        }
        else
        {
            encoded += '%';
            encoded += static_cast<char>(toHex(ch >> 4));
            encoded += static_cast<char>(toHex(ch & 0x0F));
        }
    }


    return encoded;
}

std::string urlDecode(const std::string &url)
{
    std::string decoded;
    decoded.reserve(url.size());

    for (int i = 0; i < url.size(); ++i)
    {
        if ('%' != url[i])
        {
            // 普通内容直接放入结果；路径中的 '+' 就是加号，不转换为空格。
            decoded.push_back(url[i]);
            continue;
        }

        // 遇到 '%' 后，需要再读取两位十六进制数字。
        if (url.size() - i < 3) throw std::invalid_argument("urlDecode(): incomplete URL percent escape");

        // 先把这两位字符转换为数值，再按“高位 * 16 + 低位”合成一个字节。
        unsigned char high = fromHex(static_cast<unsigned char>(url[1 + i]));
        unsigned char low = fromHex(static_cast<unsigned char>(url[2 + i]));
        // 例如 "%20" 得到 2 * 16 + 0 = 32，将对应的空格放入结果。
        decoded.push_back(static_cast<char>(16 * high + low));

        // 后两位已经处理完，跳过它们，下一轮继续读取后面的内容。
        i += 2;
    }


    return decoded;
}

std::string escapeHtmlText(const std::string &text)
{
    std::string escaped;

    for (auto &ch : text)
    {
        switch (ch)
        {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&#39;";
                break;
            default:
                escaped += ch;
                break;
        }
    }


    return escaped;
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
