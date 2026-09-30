load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "jwks_provider_lib",
    srcs = ["jwks_provider.cc"],
    hdrs = ["jwks_provider.h"],
    deps = [
        "//envoy/api:api_interface",
        "//envoy/router:router_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/stats:stats_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:lock_guard_lib",
        "//source/common/common:thread_lib",
        "//source/common/config:datasource_lib",
        "//source/common/http:utility_lib",
        "//source/common/jwt:jwt_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:retry_policy_lib",
        "//source/extensions/filters/http/common:jwks_fetcher_lib",
        "//source/extensions/filters/http/jwt_authn:jwks_async_fetcher_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/common/jwks/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)
