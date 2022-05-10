load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "fragment",
    srcs = ["fragment.cc"],
    hdrs = ["fragment.h"],
    deps = [
        "//source/extensions/common/async_files",
    ],
)

envoy_cc_library(
    name = "file_system_buffer_lib",
    srcs = [
        "filter.cc",
        "filter_config.cc",
    ],
    hdrs = [
        "filter.h",
        "filter_config.h",
    ],
    deps = [
        ":fragment",
        "//envoy/server:instance_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/file_system_buffer/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":file_system_buffer_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/file_system_buffer/v3:pkg_cc_proto",
    ],
)
