load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "substitution_formatter_lib",
    srcs = ["substitution_formatter.cc"],
    hdrs = ["substitution_formatter.h"],
    deps = [
        "//envoy/formatter:substitution_formatter_interface",
        "//source/common/json:json_loader_lib",
    ],
)

envoy_cc_library(
    name = "fluentd_access_log_lib",
    srcs = ["fluentd_access_log_impl.cc"],
    hdrs = ["fluentd_access_log_impl.h"],
    deps = [
        ":substitution_formatter_lib",
        "//envoy/access_log:access_log_interface",
        "//source/common/access_log:access_log_lib",
        "//source/extensions/access_loggers/common:access_log_base",
        "@com_github_msgpack_cpp//:msgpack",
        "@envoy_api//envoy/extensions/access_loggers/fluentd/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":fluentd_access_log_lib",
        ":substitution_formatter_lib",
        "//envoy/access_log:access_log_config_interface",
        "//envoy/registry",
        "//source/common/config:config_provider_lib",
        "//source/common/formatter:substitution_format_string_lib",
        "//source/common/protobuf",
    ],
)
