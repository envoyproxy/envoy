load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "default_config_dump_filter",
    srcs = ["default_config_dump_filter.cc"],
    hdrs = ["default_config_dump_filter.h"],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//envoy/registry",
        "//envoy/server:config_dump_config_interface",
        "//source/common/common:regex_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/extensions/admin/config_dump_filter/v3alpha:pkg_cc_proto",
    ],
)
