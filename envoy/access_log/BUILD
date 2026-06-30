load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "access_log_interface",
    hdrs = ["access_log.h"],
    deps = [
        "//envoy/config:typed_config_interface",
        "//envoy/filesystem:filesystem_interface",
        "//envoy/formatter:http_formatter_context_interface",
        "//envoy/stream_info:stream_info_interface",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "access_log_config_interface",
    hdrs = ["access_log_config.h"],
    deps = [
        ":access_log_interface",
        "//envoy/common:pure_lib",
        "//envoy/config:typed_config_interface",
        "//envoy/formatter:substitution_formatter_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/protobuf",
    ],
)
