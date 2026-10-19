includes("config.lua")
includes("@builtin/xpack")


local project_name = "dlog-cloud-storage"
local macro_prefix = "DLOG_CLOUD_STORAGE"
local version = "0.0.0"

-- 项目包依赖的唯一清单，每项填写 XMake 依赖规格字符串，例如 "fmt >=10.2.1"。common 对所有平台生效；windows 和 linux 分别只在对应目标平台生效。
-- 此表同时驱动 add_requires 和 target 的 add_packages；新增依赖只需在这里登记。
local package_dependencies = {
    common = {"bundle", "jsoncpp", "cpp-base64 2.rc.09"},
    windows = {},
    linux = {}
}

add_repositories("davidingplus https://github.com/DavidingPlus/xmake-repo.git")


set_version(version)

set_xmakever("3.0.9")
set_project(project_name)
set_description("A Cloud Storage Application Powered By dlog.")
set_languages("cxx17")

add_rules("mode.debug", "mode.release")

set_configvar("MACRO_PREFIX", macro_prefix)
set_configdir("$(builddir)/config/")
add_configfiles("src/config.h.in", "src/globalmacros.h.in")


-- 配置当前平台时只启用 common 和当前平台的依赖。
local active_package_dependencies = {}

local function enable_package_dependencies(requirements)
    for _, requirement in ipairs(requirements) do
        add_requires(requirement, {
            configs = {shared = true, debug = is_mode("debug")}
        })
        table.insert(active_package_dependencies, requirement)
    end
end

local function package_name_from_spec(requirement)
    local name = requirement:match("^%s*([^%s<>=~!]+)")
    if not name then
        os.raise("invalid package requirement: %s", requirement)
    end
    return name
end

enable_package_dependencies(package_dependencies.common)

if is_plat("windows") then
    enable_package_dependencies(package_dependencies.windows)
elseif is_plat("linux") then
    enable_package_dependencies(package_dependencies.linux)
end


option("with_gtest")
    set_default(false)
    set_showmenu(true)
    set_description("Enable GoogleTest-based unit tests.")
option_end()

option("install_in_place")
    set_default(true)
    set_showmenu(true)
    set_description("Install to $(builddir)/$(plat)/$(arch)/$(mode)/install by default.")
option_end()

define_current_platform_options()

local install_in_place = get_config("install_in_place")

if install_in_place == nil then
    install_in_place = true
end

if install_in_place then
    -- 请用 xmake install 安装。
    set_installdir("$(builddir)/$(plat)/$(arch)/$(mode)/install")
end


-- 跨平台工具静态库供主程序、单元测试和开发示例共用，不安装到应用发布目录。
target(project_name .. "-utils-static")
    set_default(false)
    set_kind("static")

    apply_current_platform_target_config()

    for _, requirement in ipairs(active_package_dependencies) do
        add_packages(package_name_from_spec(requirement), {public = true})
    end

    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/lib/")

    add_files("src/utils/**.cpp")

    -- public 只用于向项目内依赖此库的目标传递配置，不代表对外发布头文件。
    add_includedirs("$(builddir)/config/", {public = true})
    add_includedirs("src", os.dirs("src/**"), {public = true})
target_end()


-- 服务端仅在 Linux 目标平台下构建。
if is_plat("linux") then
    target(project_name .. "-server")
        set_kind("binary")

        apply_current_platform_target_config()

        add_deps(project_name .. "-utils-static")
        add_files("src/server/**.cpp")
        set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/bin/")

        -- 关闭为项目内动态库依赖自动添加的 RPATH，不影响下面的手动设置。当前内部库为静态库，此项不是必需；保留以显式控制自动添加行为。
        set_policy("build.rpath", false)

        -- 安装和打包时保留链接阶段的 RPATH，避免清理或改写它而依赖额外工具。
        set_policy("install.rpath", false)

        -- $ORIGIN 是可执行文件所在目录，bin 中的程序据此查找同级 lib 目录。runpath = false 明确使用 RPATH，使该路径也用于查找间接动态库依赖；省略此参数时，由链接器默认行为决定生成 RPATH 还是 RUNPATH。
        add_rpathdirs("$ORIGIN/../lib", {runpath = false})
    target_end()

    -- 只发布服务端程序及其运行依赖，内部静态库、测试和示例不单独打包。
    xpack(project_name .. "-server")
        set_formats("targz")
        set_basename(project_name .. "-server-$(plat)-$(arch)-$(mode)-v$(version)")
        set_title(project_name .. " Server")
        set_description("A Cloud Storage Application Powered By dlog.")
        add_targets(project_name .. "-server")
    xpack_end()
end


includes("snippet")

if get_config("with_gtest") then
    includes("test")
end
