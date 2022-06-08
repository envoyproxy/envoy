load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_contrib_package",
)
load("@bazel_skylib//lib:selects.bzl", "selects")
load("@rules_foreign_cc//foreign_cc:defs.bzl", "make")

licenses(["notice"])  # Apache 2

envoy_contrib_package()

selects.config_setting_group(
    name = "linux_x86_64_gcc_build",
    match_all = [
        "//bazel:linux_x86_64",
        "//bazel:gcc_build",
    ],
)

make(
    name = "dlb",
    includes = [],
    lib_source = "@intel_dlb//:libdlb",
    out_static_libs = ["libdlb.a"],
    postfix_script = "mv libdlb.a $INSTALLDIR/lib; cp -L *.h $INSTALLDIR/include",
    tags = ["skip_on_windows"],
    targets = ["libdlb.a"],
)

envoy_cc_contrib_extension(
    name = "connection_balancer",
    srcs = [
        "connection_balancer_impl.cc",
    ],
    hdrs = ["connection_balancer_impl.h"],
    defines = select({
        ":linux_x86_64_gcc_build": [],
        "//conditions:default": [
            "DLB_DISABLED=1",
        ],
    }),
    deps = [
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//envoy/server:filter_config_interface",
        "//source/common/common:logger_lib",
        "//source/common/network:connection_balancer_lib",
        "//source/common/protobuf:utility_lib",
        "//source/server:active_tcp_listener",
        "@envoy_api//contrib/envoy/extensions/dlb/v3alpha:pkg_cc_proto",
    ] + select({
        ":linux_x86_64_gcc_build": [
            ":dlb",
        ],
        "//conditions:default": [
        ],
    }),
)
