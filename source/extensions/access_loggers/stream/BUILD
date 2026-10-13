load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//source/common/config:config_provider_lib",
        "//source/common/formatter:substitution_format_string_lib",
        "//source/common/protobuf",
        "//source/extensions/access_loggers/common:file_access_log_lib",
        "//source/extensions/access_loggers/common:stream_access_log_common_impl_lib",
        "@envoy_api//envoy/extensions/access_loggers/stream/v3:pkg_cc_proto",
    ],
)
