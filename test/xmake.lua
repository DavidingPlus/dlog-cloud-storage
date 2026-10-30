if not get_config("with_gtest") then
    return
end

includes("config.lua")


add_requires("gtest")


-- 测试分组的唯一清单，每项对应 test 下的一个目录。common 对所有平台生效；win32 和 linux 分别只在 Windows 和 Linux 目标平台生效。
local gtest_groups = {
    common = {"testrun", "utils", "third_party"},
    win32 = {},
    linux = {"server"}
}

local active_gtest_groups = {}

local function enable_gtest_groups(groups)
    for _, group in ipairs(groups) do
        table.insert(active_gtest_groups, group)
    end
end

enable_gtest_groups(gtest_groups.common)

if is_plat("windows") then
    enable_gtest_groups(gtest_groups.win32)
elseif is_plat("linux") then
    enable_gtest_groups(gtest_groups.linux)
end


-- 请用 xmake test 执行测试。
target("tests")
    set_default(false)

    set_kind("binary")
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/test")

    add_packages("gtest")
    add_packages("bundle", "cpp-base64", "jsoncpp", "libevent", "utfcpp")

    add_deps("dlog-cloud-storage-utils-static")
    if is_plat("linux") then
        add_deps("dlog-cloud-storage-server-static")

        -- 服务端测试同步运行配置和页面，资源布局与服务端程序保持一致。
        on_config(function (target)
            local configdir = path.join(target:targetdir(), "config")
            local webdir = path.join(target:targetdir(), "web")
            os.mkdir(configdir)
            os.mkdir(webdir)
            os.cp(path.join(os.projectdir(), "src/server/config/server.conf"), path.join(configdir, "server.conf"))
            os.cp(path.join(os.projectdir(), "src/server/web/template.html"), path.join(webdir, "template.html"))
        end)
    end

    before_build(function ()
        os.tryrm("$(builddir)/$(plat)/$(arch)/$(mode)/test/gtest")
        os.mkdir("$(builddir)/$(plat)/$(arch)/$(mode)/test/gtest")
    end)

    on_install(function () end)
    apply_current_platform_target_config()

    add_files("main.cpp")
    apply_gtest_summary_config("tests", active_gtest_groups)

    for _, group in ipairs(active_gtest_groups) do
        local options = {
            realtime_output = true,
            files = {group .. "/*.cpp"}
        }
        -- GCC/Clang 仅在服务端测试中放开私有成员访问，业务源码和正式构建不受影响。
        if group == "server" then
            options.cxxflags = {"-fno-access-control"}
        end
        add_tests(group, options)
    end
target_end()


-- 页面脚本使用 Node.js 内置测试模块，作为独立测试项随 xmake test 一起执行。
if is_plat("linux") then
    target("tests-server-web")
        set_default(false)
        set_kind("phony")
        on_install(function () end)

        add_tests("upload")
        on_test(function ()
            import("lib.detect.find_tool")
            local node = find_tool("node")
            assert(node, "Node.js is required to run tests-server-web/upload")
            os.vrunv(node.program, {path.join(os.projectdir(), "test/server/web/upload_test.js")})
            return true
        end)
    target_end()
end
