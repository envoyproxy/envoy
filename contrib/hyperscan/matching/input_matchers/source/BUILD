load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_cmake",
    "envoy_contrib_package",
)
load(
    "//contrib:all_contrib_extensions.bzl",
    "envoy_contrib_linux_aarch64_constraints",
    "envoy_contrib_linux_x86_64_constraints",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cmake(
    name = "hyperscan",
    build_data = ["@org_boost//:header"],
    cache_entries = {
        "BOOST_ROOT": "$EXT_BUILD_ROOT/external/org_boost",
        "BUILD_AVX512": "on",
        "BUILD_AVX512VBMI": "on",
        "BUILD_EXAMPLES": "off",
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "FAT_RUNTIME": "on",
        "RAGEL": "$$EXT_BUILD_DEPS$$/ragel/bin/ragel",
    },
    default_cache_entries = {},
    lib_source = "@io_hyperscan//:all",
    out_static_libs = ["libhs.a"],
    tags = ["skip_on_windows"],
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
    deps = [
        "//bazel/foreign_cc:ragel",
    ],
)

envoy_cmake(
    name = "vectorscan",
    build_data = ["@org_boost//:header"],
    cache_entries = {
        "BOOST_ROOT": "$EXT_BUILD_ROOT/external/org_boost",
        #        "BUILD_SVE2_BITPERM": "on",
        #        "BUILD_SVE2": "on",
        #        "BUILD_SVE": "on",
        "BUILD_EXAMPLES": "off",
        "BUILD_SHARED_LIBS": "off",
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "FAT_RUNTIME": "off",
        "SQLITE_SKIP_CHECK": "on",
        "RAGEL": "$$EXT_BUILD_DEPS$$/ragel/bin/ragel",
    },
    default_cache_entries = {},
    lib_source = "@io_vectorscan//:all",
    out_static_libs = ["libhs.a"],
    tags = ["skip_on_windows"],
    target_compatible_with = envoy_contrib_linux_aarch64_constraints(),
    deps = [
        "//bazel/foreign_cc:ragel",
    ],
)

envoy_cc_library(
    name = "hyperscan_matcher_lib",
    srcs = ["matcher.cc"],
    hdrs = ["matcher.h"],
    deps = [
        ":hyperscan",
        "//envoy/common:regex_interface",
        "//envoy/matcher:matcher_interface",
        "//envoy/thread_local:thread_local_interface",
    ],
)

envoy_cc_library(
    name = "vectorscan_matcher_lib",
    srcs = ["matcher.cc"],
    hdrs = ["matcher.h"],
    deps = [
        ":vectorscan",
        "//envoy/common:regex_interface",
        "//envoy/matcher:matcher_interface",
        "//envoy/thread_local:thread_local_interface",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    defines = select({
        "//bazel:linux_x86_64": [],
        "//bazel:linux_aarch64": [],
        "//conditions:default": [
            "HYPERSCAN_DISABLED=1",
        ],
    }),
    deps = [
        "//envoy/matcher:matcher_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//contrib/envoy/extensions/matching/input_matchers/hyperscan/v3alpha:pkg_cc_proto",
    ] + select({
        "//bazel:linux_x86_64": [
            ":hyperscan_matcher_lib",
        ],
        "//bazel:linux_aarch64": [
            ":vectorscan_matcher_lib",
        ],
        "//conditions:default": [
        ],
    }),
)
