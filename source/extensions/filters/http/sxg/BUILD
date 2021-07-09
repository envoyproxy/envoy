load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)
load(
    "@rules_foreign_cc//tools/build_defs:cmake.bzl",
    "cmake_external",
)

licenses(["notice"])  # Apache 2

#SXG HTTP filter

envoy_extension_package()

cmake_external(
    name = "libsxg",
    cache_entries = {
        "CMAKE_BUILD_TYPE": "Release",
        "SXG_BUILD_EXECUTABLES": "off",
        "SXG_BUILD_SHARED": "off",
        "SXG_BUILD_STATIC": "on",
        "SXG_WITH_CERT_CHAIN": "off",
        "RUN_TEST": "off",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "CMAKE_TRY_COMPILE_TARGET_TYPE": "STATIC_LIBRARY",
    },
    lib_source = "@com_github_google_libsxg//:all",
    static_libraries = ["libsxg.a"],
    deps = ["@boringssl//:ssl"],
)

envoy_cc_library(
    name = "sxg_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "libsxg",
        "//envoy/server:filter_config_interface",
        "//source/common/config:datasource_lib",
        "//source/common/http:codes_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@boringssl//:ssl",
        "@envoy_api//envoy/extensions/filters/http/sxg/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":sxg_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/sxg/v3alpha:pkg_cc_proto",
    ],
)
