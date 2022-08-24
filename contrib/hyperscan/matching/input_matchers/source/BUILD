load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_cmake",
    "envoy_contrib_package",
)
load(
    "//bazel:envoy_internal.bzl",
    "envoy_external_dep_path",
)
load(
    "//contrib:all_contrib_extensions.bzl",
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
        "CMAKE_INSTALL_LIBDIR": "lib",
        "RAGEL": "$EXT_BUILD_DEPS/ragel/bin/ragel",
    },
    env = select({
        "//bazel:clang_build": {
            "CXXFLAGS": "-Wno-error=unused-but-set-variable -Wno-error=bitwise-instead-of-logical",
        },
        "//conditions:default": {},
    }),
    lib_source = "@io_hyperscan//:all",
    out_static_libs = ["libhs.a"],
    tags = ["skip_on_windows"],
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
    deps = [
        envoy_external_dep_path("ragel"),
    ],
)

envoy_cc_library(
    name = "matcher_lib",
    srcs = ["matcher.cc"],
    hdrs = ["matcher.h"],
    deps = [
        ":hyperscan",
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
            ":matcher_lib",
        ],
        "//conditions:default": [
        ],
    }),
)
