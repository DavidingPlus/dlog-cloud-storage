#include <filesystem>
#include <iostream>


namespace fs = std::filesystem;


int main()
{
    // 五个接口均返回新的 path，不修改传入路径。三个 lexically_* 接口只处理路径文本，不访问磁盘、不要求路径存在，也不解析符号链接。

    // 1. lexically_normal()：化简 .、可消去的 .. 和多余分隔符，相对路径仍为相对路径。
    // 本例返回 data/report.txt。
    const fs::path path = "data/./cache/../report.txt";
    std::cout << "lexically_normal: " << path.lexically_normal() << std::endl;

    // 2. lexically_relative(base)：计算从 base 到当前路径的相对路径，无法计算时返回空路径。
    // 本例从 data/cache 到 data/report.txt，返回 ../report.txt。
    const fs::path target = "data/report.txt";
    const fs::path base = "data/cache";
    std::cout << "lexically_relative: " << target.lexically_relative(base) << std::endl;

    // 3. lexically_proximate(base)：能算出相对路径时与 relative 相同，否则返回原路径。
    // relative 和 proximate 不会先标准化输入，需要时先对两侧调用 lexically_normal()。
    std::cout << "lexically_proximate: " << target.lexically_proximate(base) << std::endl;
    const auto currentDir = fs::current_path();
    const auto absolute = currentDir / target;
    std::cout << "relative (absolute vs relative): " << absolute.lexically_relative(base) << std::endl;   // 空路径。
    std::cout << "proximate (absolute vs relative): " << absolute.lexically_proximate(base) << std::endl; // 原绝对路径。

    // 4. fs::canonical(p)：访问磁盘、解析符号链接并消去 . 和 ..，返回规范绝对路径。
    // 要求路径存在，否则抛出 filesystem_error；本例使用已存在的当前目录。
    std::cout << "canonical: " << fs::canonical(currentDir / ".") << std::endl;

    // 5. fs::weakly_canonical(p)：解析已有前缀中的符号链接，再拼接后缀并对结果做词法标准化。
    // 允许后缀不存在，因此这里不需要创建 new-dir 或文件。
    std::cout << "weakly_canonical: " << fs::weakly_canonical(currentDir / "new-dir/./report.txt") << std::endl;
}
