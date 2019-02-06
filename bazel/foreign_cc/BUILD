licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_cmake_external", "envoy_package")

envoy_package()

envoy_cmake_external(
    name = "ares",
    cache_entries = {
        "CARES_SHARED": "no",
        "CARES_STATIC": "on",
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
    },
    lib_source = "@com_github_c_ares_c_ares//:all",
    postfix_script = select({
        "//bazel:windows_dbg_build": "cp $BUILD_TMPDIR/CMakeFiles/c-ares.dir/c-ares.pdb $INSTALLDIR/lib/c-ares.pdb",
        "//conditions:default": "",
    }),
    static_libraries = select({
        "//bazel:windows_x86_64": ["cares.lib"],
        "//conditions:default": ["libcares.a"],
    }),
)

envoy_cmake_external(
    name = "benchmark",
    cache_entries = {
        "BENCHMARK_ENABLE_GTEST_TESTS": "OFF",
        "BENCHMARK_ENABLE_TESTING": "OFF",
    },
    lib_source = "@com_github_google_benchmark//:all",
    postfix_script = "mkdir -p $INSTALLDIR/include/testing/base/public && cp $BUILD_TMPDIR/$INSTALL_PREFIX/include/benchmark/benchmark.h $INSTALLDIR/include/testing/base/public/benchmark.h",
    static_libraries = select({
        "//bazel:windows_x86_64": ["benchmark.lib"],
        "//conditions:default": ["libbenchmark.a"],
    }),
)

envoy_cmake_external(
    name = "event",
    cache_entries = {
        "EVENT__DISABLE_OPENSSL": "on",
        "EVENT__DISABLE_REGRESS": "on",
        "CMAKE_BUILD_TYPE": "Release",
    },
    lib_source = "@com_github_libevent_libevent//:all",
    postfix_script = select({
        "//bazel:windows_dbg_build": "cp $BUILD_TMPDIR/CMakeFiles/event.dir/event.pdb $INSTALLDIR/lib/event.pdb",
        "//conditions:default": "",
    }),
    static_libraries = select({
        "//bazel:windows_x86_64": ["event.lib"],
        "//conditions:default": ["libevent.a"],
    }),
)

envoy_cmake_external(
    name = "nghttp2",
    cache_entries = {
        "ENABLE_STATIC_LIB": "on",
        "ENABLE_LIB_ONLY": "on",
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        "CMAKE_INSTALL_LIBDIR": "lib",
    },
    lib_source = "@com_github_nghttp2_nghttp2//:all",
    postfix_script = select({
        "//bazel:windows_dbg_build": "cp $BUILD_TMPDIR/lib/CMakeFiles/nghttp2_static.dir/nghttp2_static.pdb $INSTALLDIR/lib/nghttp2_static.pdb",
        "//conditions:default": "",
    }),
    static_libraries = select({
        "//bazel:windows_x86_64": ["nghttp2_static.lib"],
        "//conditions:default": ["libnghttp2_static.a"],
    }),
)

envoy_cmake_external(
    name = "yaml",
    cache_entries = {
        "YAML_CPP_BUILD_TESTS": "off",
        "YAML_CPP_BUILD_TOOLS": "off",
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
    },
    lib_source = "@com_github_jbeder_yaml_cpp//:all",
    static_libraries = select({
        "//bazel:windows_dbg_build": ["libyaml-cppmdd.lib"],
        "//bazel:windows_fastbuild_build": ["libyaml-cppmd.lib"],
        "//bazel:windows_opt_build": ["libyaml-cppmd.lib"],
        "//conditions:default": ["libyaml-cpp.a"],
    }),
)

envoy_cmake_external(
    name = "zlib",
    cache_entries = {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
    },
    lib_source = "@com_github_madler_zlib//:all",
    postfix_script = select({
        "//bazel:windows_dbg_build": "cp $BUILD_TMPDIR/CMakeFiles/zlibstatic.dir/zlibstatic.pdb $INSTALLDIR/lib/zlibstatic.pdb",
        "//conditions:default": "",
    }),
    static_libraries = select({
        "//bazel:windows_dbg_build": ["zlibstaticd.lib"],
        "//bazel:windows_fastbuild_build": ["zlibstatic.lib"],
        "//bazel:windows_opt_build": ["zlibstatic.lib"],
        "//conditions:default": ["libz.a"],
    }),
)
