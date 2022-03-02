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
        "BUILD_EXAMPLES": "off",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "RAGEL": "$EXT_BUILD_DEPS/ragel/bin/ragel",
    },
    lib_source = "@io_hyperscan//:all",
    out_static_libs = ["libhs.a"],
    tags = ["skip_on_windows"],
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
    deps = [
        envoy_external_dep_path("ragel"),
    ],
)

envoy_cc_library(
    name = "hyperscan_lib",
    srcs = ["matcher.cc"],
    hdrs = ["matcher.h"],
    deps = [
        ":hyperscan",
        "//envoy/matcher:matcher_interface",
        "//envoy/thread_local:thread_local_interface",
        "@envoy_api//contrib/envoy/extensions/matching/input_matchers/hyperscan/v3alpha:pkg_cc_proto",
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
            ":hyperscan_lib",
        ],
        "//conditions:default": [
        ],
    }),
)
