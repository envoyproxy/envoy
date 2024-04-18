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
    extra_visibility = [
        "//test/integration:__subpackages__",
        "//test/config_test:__subpackages__",
    ],
    deps = [
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/config:utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/matcher:matcher_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/common/matching/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/common/matcher/action/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)
