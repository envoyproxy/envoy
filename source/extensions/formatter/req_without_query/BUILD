load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

# Access log formatter that strips query string from request path
# Public docs: TODO(tsaarni)

envoy_cc_library(
    name = "req_without_query_lib",
    srcs = ["req_without_query.cc"],
    hdrs = ["req_without_query.h"],
    deps = [
        "//source/common/formatter:substitution_format_utility_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//source/extensions/formatter/req_without_query:req_without_query_lib",
        "@envoy_api//envoy/extensions/formatter/req_without_query/v3:pkg_cc_proto",
    ],
)
