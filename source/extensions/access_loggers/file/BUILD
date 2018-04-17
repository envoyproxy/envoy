licenses(["notice"])  # Apache 2
# Access log implementation that writes to a file.
# Public docs: docs/root/configuration/access_log.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "file_access_log_lib",
    srcs = ["file_access_log_impl.cc"],
    hdrs = ["file_access_log_impl.h"],
    deps = [
        "//include/envoy/access_log:access_log_interface",
        "//source/common/http:header_map_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":file_access_log_lib",
        "//include/envoy/registry",
        "//include/envoy/server:access_log_config_interface",
        "//source/common/access_log:access_log_formatter_lib",
        "//source/common/protobuf",
        "//source/extensions/access_loggers:well_known_names",
        "@envoy_api//envoy/config/filter/accesslog/v2:accesslog_cc",
    ],
)
