licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@rules_foreign_cc//tools/build_defs:cmake.bzl", "cmake_external")

envoy_package()

config_setting(
    name = "windows_x86_64",
    values = {"cpu": "x64_windows"},
)

config_setting(
    name = "windows_dbg_build",
    values = {
        "cpu": "x64_windows",
        "compilation_mode": "dbg",
    },
)

config_setting(
    name = "windows_fastbuild_build",
    values = {
        "cpu": "x64_windows",
        "compilation_mode": "fastbuild",
    },
)

config_setting(
    name = "windows_opt_build",
    values = {
        "cpu": "x64_windows",
        "compilation_mode": "opt",
    },
)

cmake_external(
    name = "ares",
    cache_entries = {
        "CARES_SHARED": "no",
        "CARES_STATIC": "on",
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
    },
    cmake_options = ["-GNinja"],
    generate_crosstool_file = select({
        ":windows_x86_64": True,
        "//conditions:default": False,
    }),
    lib_source = "@com_github_c_ares_c_ares//:all",
    make_commands = [
        "ninja",
        "ninja install",
    ],
    postfix_script = select({
        ":windows_dbg_build": "cp $BUILD_TMPDIR/CMakeFiles/c-ares.dir/c-ares.pdb $INSTALLDIR/lib/c-ares.pdb",
        "//conditions:default": "",
    }),
    static_libraries = select({
        ":windows_x86_64": ["cares.lib"],
        "//conditions:default": ["libcares.a"],
    }),
)

cmake_external(
    name = "benchmark",
    cache_entries = {
        "BENCHMARK_ENABLE_GTEST_TESTS": "OFF",
        "BENCHMARK_ENABLE_TESTING": "OFF",
    },
    cmake_options = ["-GNinja"],
    generate_crosstool_file = select({
        ":windows_x86_64": True,
        "//conditions:default": False,
    }),
    lib_source = "@com_github_google_benchmark//:all",
    make_commands = [
        "ninja",
        "ninja install",
    ],
    postfix_script = "mkdir -p $INSTALLDIR/include/testing/base/public && cp $BUILD_TMPDIR/$INSTALL_PREFIX/include/benchmark/benchmark.h $INSTALLDIR/include/testing/base/public/benchmark.h",
    static_libraries = select({
        ":windows_x86_64": ["benchmark.lib"],
        "//conditions:default": ["libbenchmark.a"],
    }),
)

cmake_external(
    name = "event",
    cache_entries = {
        "EVENT__DISABLE_OPENSSL": "on",
        "EVENT__DISABLE_REGRESS": "on",
        "CMAKE_BUILD_TYPE": "Release",
    },
    cmake_options = ["-GNinja"],
    generate_crosstool_file = select({
        ":windows_x86_64": True,
        "//conditions:default": False,
    }),
    lib_source = "@com_github_libevent_libevent//:all",
    make_commands = [
        "ninja",
        "ninja install",
    ],
    postfix_script = select({
        ":windows_dbg_build": "cp $BUILD_TMPDIR/CMakeFiles/event.dir/event.pdb $INSTALLDIR/lib/event.pdb",
        "//conditions:default": "",
    }),
    static_libraries = select({
        ":windows_x86_64": ["event.lib"],
        "//conditions:default": ["libevent.a"],
    }),
)

cmake_external(
    name = "nghttp2",
    cache_entries = {
        "ENABLE_STATIC_LIB": "on",
        "ENABLE_LIB_ONLY": "on",
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        "CMAKE_INSTALL_LIBDIR": "lib",
    },
    cmake_options = ["-GNinja"],
    generate_crosstool_file = select({
        ":windows_x86_64": True,
        "//conditions:default": False,
    }),
    lib_source = "@com_github_nghttp2_nghttp2//:all",
    make_commands = [
        "ninja",
        "ninja install",
    ],
    postfix_script = select({
        ":windows_dbg_build": "cp $BUILD_TMPDIR/lib/CMakeFiles/nghttp2_static.dir/nghttp2_static.pdb $INSTALLDIR/lib/nghttp2_static.pdb",
        "//conditions:default": "",
    }),
    static_libraries = select({
        ":windows_x86_64": ["nghttp2_static.lib"],
        "//conditions:default": ["libnghttp2_static.a"],
    }),
)

cmake_external(
    name = "yaml",
    cache_entries = {
        "YAML_CPP_BUILD_TESTS": "off",
        "YAML_CPP_BUILD_TOOLS": "off",
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
    },
    cmake_options = ["-GNinja"],
    generate_crosstool_file = select({
        ":windows_x86_64": True,
        "//conditions:default": False,
    }),
    lib_source = "@com_github_jbeder_yaml_cpp//:all",
    make_commands = [
        "ninja",
        "ninja install",
    ],
    static_libraries = select({
        ":windows_dbg_build": ["libyaml-cppmdd.lib"],
        ":windows_fastbuild_build": ["libyaml-cppmd.lib"],
        ":windows_opt_build": ["libyaml-cppmd.lib"],
        "//conditions:default": ["libyaml-cpp.a"],
    }),
)

cmake_external(
    name = "zlib",
    cache_entries = {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
    },
    cmake_options = ["-GNinja"],
    generate_crosstool_file = select({
        ":windows_x86_64": True,
        "//conditions:default": False,
    }),
    lib_source = "@com_github_madler_zlib//:all",
    make_commands = [
        "ninja",
        "ninja install",
    ],
    postfix_script = select({
        ":windows_dbg_build": "cp $BUILD_TMPDIR/CMakeFiles/zlibstatic.dir/zlibstatic.pdb $INSTALLDIR/lib/zlibstatic.pdb",
        "//conditions:default": "",
    }),
    static_libraries = select({
        ":windows_dbg_build": ["zlibstaticd.lib"],
        ":windows_fastbuild_build": ["zlibstatic.lib"],
        ":windows_opt_build": ["zlibstatic.lib"],
        "//conditions:default": ["libz.a"],
    }),
)
