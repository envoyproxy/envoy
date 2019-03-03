licenses(["notice"])  # Apache 2

# L7 HTTP filter which implements CSRF processing (https://www.owasp.org/index.php/Cross-Site_Request_Forgery_(CSRF))
# Public docs: docs/root/configuration/http_filters/csrf_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "csrf_filter_lib",
    srcs = ["csrf_filter.cc"],
    hdrs = ["csrf_filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:enum_to_int",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:empty_http_filter_config_lib",
        "//source/extensions/filters/http/csrf:csrf_filter_lib",
    ],
)
