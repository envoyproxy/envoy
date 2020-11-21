load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Access log implementation that writes to a file.
# Public docs: docs/root/configuration/access_log.rst

envoy_extension_package()

envoy_cc_library(
    name = "file_access_log_lib",
    srcs = ["file_access_log_impl.cc"],
    hdrs = ["file_access_log_impl.h"],
    # The file based access logger is core code.
    visibility = ["//visibility:public"],
    deps = [
        "//source/extensions/access_loggers/common:access_log_base",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    # TODO(#9953) determine if this is core or should be cleaned up.
    extra_visibility = [
        "//test:__subpackages__",
    ],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":file_access_log_lib",
        "//include/envoy/registry",
        "//source/common/formatter:substitution_format_string_lib",
        "//source/common/protobuf",
        "//source/extensions/access_loggers:well_known_names",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
    ],
)
