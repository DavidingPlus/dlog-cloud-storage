includes("config.lua")


local project_name = "dlog-cloud-storage"
local macro_prefix = "DLOG_CLOUD_STORAGE"
local version = "0.0.0"

-- 项目包依赖的唯一清单，每项填写 XMake 依赖规格字符串，例如 "fmt >=10.2.1"。common 对所有平台生效；windows 和 linux 分别只在对应目标平台生效。
-- 此表同时驱动 add_requires 和 target 的 add_packages；新增依赖只需在这里登记。
local package_dependencies = {
    common = {},
    windows = {},
    linux = {}
}


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
        add_requires(requirement)
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


-- 内部静态库供主程序、单元测试和开发示例共用，不安装到应用发布目录。
target(project_name .. "-server-static")
    set_default(false)
    set_kind("static")

    apply_current_platform_target_config()

    for _, requirement in ipairs(active_package_dependencies) do
        add_packages(package_name_from_spec(requirement), {public = true})
    end

    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/lib/")

    -- 匹配服务端源码，排除主程序入口。
    add_files("src/server/**.cpp|main.cpp")

    -- public 只用于向项目内依赖此库的目标传递配置，不代表对外发布头文件。
    add_includedirs("$(builddir)/config/", {public = true})
    add_includedirs("src", os.dirs("src/**"), {public = true})
target_end()


target(project_name .. "-server")
    set_kind("binary")

    apply_current_platform_target_config()

    add_deps(project_name .. "-server-static")
    add_files("src/server/main.cpp")
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/bin/")

    -- TODO 完善打包流程。
    on_install(function () end)
target_end()


includes("snippet")

if get_config("with_gtest") then
    includes("test")
end
