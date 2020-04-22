licenses(["notice"])  # Apache 2

# Access log implementation that writes to a file.
# Public docs: docs/root/configuration/access_log.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "file_access_log_lib",
    srcs = ["file_access_log_impl.cc"],
    hdrs = ["file_access_log_impl.h"],
    deps = [
        "//source/extensions/access_loggers/common:access_log_base",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":file_access_log_lib",
        "//include/envoy/registry",
        "//include/envoy/server:access_log_config_interface",
        "//source/common/access_log:access_log_formatter_lib",
        "//source/common/protobuf",
        "//source/extensions/access_loggers:well_known_names",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
    ],
)
