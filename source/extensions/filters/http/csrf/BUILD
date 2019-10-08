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
        "//source/common/common:matchers_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http:well_known_names",
        "@envoy_api//envoy/config/filter/http/csrf/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/csrf:csrf_filter_lib",
    ],
)
