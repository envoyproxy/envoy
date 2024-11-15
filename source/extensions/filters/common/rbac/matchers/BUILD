load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "upstream_ip_port_lib",
    srcs = [
        "upstream_ip_port.cc",
    ],
    hdrs = [
        "upstream_ip_port.h",
    ],
    deps = [
        "//source/common/common:logger_lib",
        "//source/common/stream_info:upstream_address_lib",
        "//source/extensions/filters/common/rbac:matchers_lib",
        "//source/extensions/filters/common/rbac:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/rbac/matchers/upstream_ip_port/v3:pkg_cc_proto",
    ],
)
