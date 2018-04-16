licenses(["notice"])  # Apache 2
# External authorization L7 HTTP filter
# Public docs: TODO(saumoh): Docs needed in docs/root/configuration/http_filters

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "ext_authz",
    srcs = ["ext_authz.cc"],
    hdrs = ["ext_authz.h"],
    deps = [
        "//include/envoy/http:codes_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/http:codes_lib",
        "//source/common/router:config_lib",
        "//source/extensions/filters/common/ext_authz:ext_authz_lib",
        "@envoy_api//envoy/config/filter/http/ext_authz/v2alpha:ext_authz_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":ext_authz",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http:well_known_names",
    ],
)
