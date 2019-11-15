licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cmake_external", "envoy_package")

envoy_package()

envoy_cmake_external(
    name = "zlib",
    cache_entries = {
        "BUILD_SHARED_LIBS": "off",
        "CMAKE_CXX_COMPILER_FORCED": "on",
    },
    lib_source = "//:all",
    static_libraries = select({
        "@envoy//bazel:windows_x86_64": ["zlibstatic.lib"],
        "//conditions:default": ["libz.a"],
    }),
)

filegroup(
    name = "all",
    srcs = glob(["**"]),
)
