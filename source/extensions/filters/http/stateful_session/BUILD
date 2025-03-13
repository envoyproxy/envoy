load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "stateful_session_lib",
    srcs = ["stateful_session.cc"],
    hdrs = ["stateful_session.h"],
    deps = [
        "//envoy/http:stateful_session_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/upstream:load_balancer_interface",
        "//source/common/config:utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/stateful_session/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/stateful_session:stateful_session_lib",
        "//source/server:generic_factory_context_lib",
        "@envoy_api//envoy/extensions/filters/http/stateful_session/v3:pkg_cc_proto",
    ],
)
