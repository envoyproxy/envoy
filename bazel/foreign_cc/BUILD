licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@rules_foreign_cc//tools/build_defs:cmake.bzl", "cmake_external")
load("@foreign_cc_platform_utils//:tools.bzl", "NINJA_COMMAND", "NINJA_DEP")

envoy_package()

cmake_external(
    name = "ares",
    cache_entries = {
        "CARES_SHARED": "no",
        "CARES_STATIC": "on",
    },
    cmake_options = ["-GNinja"],
    lib_source = "@com_github_c_ares_c_ares//:all",
    make_commands = [
        NINJA_COMMAND,
        NINJA_COMMAND + " install",
    ],
    static_libraries = ["libcares.a"],
    tools_deps = NINJA_DEP,
)

cmake_external(
    name = "nghttp2",
    cache_entries = {
        "ENABLE_STATIC_LIB": "on",
        "ENABLE_LIB_ONLY": "on",
    },
    cmake_options = ["-GNinja"],
    lib_source = "@com_github_nghttp2_nghttp2//:all",
    make_commands = [
        NINJA_COMMAND,
        NINJA_COMMAND + " install",
    ],
    static_libraries = ["libnghttp2.a"],
    tools_deps = NINJA_DEP,
)
