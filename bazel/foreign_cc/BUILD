licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@rules_foreign_cc//tools/build_defs:cmake.bzl", "cmake_external")

envoy_package()

cmake_external(
    name = "ares",
    cache_entries = {
        "CARES_SHARED": "no",
        "CARES_STATIC": "on",
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
    },
    cmake_options = ["-GNinja"],
    lib_source = "@com_github_c_ares_c_ares//:all",
    make_commands = [
        "ninja",
        "ninja install",
    ],
    static_libraries = ["libcares.a"],
)

cmake_external(
    name = "event",
    cache_entries = {
        "EVENT__DISABLE_OPENSSL": "on",
        "EVENT__DISABLE_REGRESS": "on",
        "CMAKE_BUILD_TYPE": "Release",
    },
    cmake_options = ["-GNinja"],
    lib_source = "@com_github_libevent_libevent//:all",
    make_commands = [
        "ninja",
        "ninja install",
    ],
    static_libraries = ["libevent.a"],
)

cmake_external(
    name = "nghttp2",
    cache_entries = {
        "ENABLE_STATIC_LIB": "on",
        "ENABLE_LIB_ONLY": "on",
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
    },
    cmake_options = ["-GNinja"],
    lib_source = "@com_github_nghttp2_nghttp2//:all",
    make_commands = [
        "ninja",
        "ninja install",
    ],
    static_libraries = ["libnghttp2.a"],
)

cmake_external(
    name = "yaml",
    cache_entries = {
        "YAML_CPP_BUILD_TESTS": "off",
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
    },
    cmake_options = ["-GNinja"],
    lib_source = "@com_github_jbeder_yaml_cpp//:all",
    make_commands = [
        "ninja",
        "ninja install",
    ],
    static_libraries = ["libyaml-cpp.a"],
)

cmake_external(
    name = "zlib",
    cache_entries = {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
    },
    cmake_options = ["-GNinja"],
    lib_source = "@com_github_madler_zlib//:all",
    make_commands = [
        "ninja",
        "ninja install",
    ],
    static_libraries = ["libz.a"],
)
