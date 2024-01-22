load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "uri_template_lib",
    srcs = [
        "uri_template.cc",
    ],
    hdrs = [
        "uri_template.h",
    ],
    deps = [
        "//source/common/common:logger_lib",
        "//source/extensions/filters/common/rbac:matchers_lib",
        "//source/extensions/filters/common/rbac:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
