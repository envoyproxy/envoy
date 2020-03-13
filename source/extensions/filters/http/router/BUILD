licenses(["notice"])  # Apache 2

# HTTP L7 filter responsible for routing to upstream connection pools
# Public docs: docs/root/configuration/http_filters/router_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_package",
)

envoy_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        "//include/envoy/registry",
        "//source/common/router:router_lib",
        "//source/common/router:shadow_writer_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/on_demand:on_demand_update_lib",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
    ],
)
