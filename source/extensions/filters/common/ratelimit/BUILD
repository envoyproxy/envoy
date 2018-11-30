licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_cc_library(
    name = "ratelimit_lib",
    srcs = ["ratelimit_impl.cc"],
    hdrs = ["ratelimit_impl.h"],
    deps = [
        ":ratelimit_client_interface",
        "//include/envoy/grpc:async_client_interface",
        "//include/envoy/grpc:async_client_manager_interface",
        "//include/envoy/ratelimit:ratelimit_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:headers_lib",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//envoy/api/v2/ratelimit:ratelimit_cc",
        "@envoy_api//envoy/config/ratelimit/v2:rls_cc",
        "@envoy_api//envoy/service/ratelimit/v2:rls_cc",
    ],
)

envoy_cc_library(
    name = "ratelimit_registration_lib",
    srcs = ["ratelimit_registration.cc"],
    hdrs = ["ratelimit_registration.h"],
    deps = [
        ":ratelimit_client_interface",
        ":ratelimit_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/server:instance_interface",
        "//source/common/common:assert_lib",
        "@envoy_api//envoy/config/bootstrap/v2:bootstrap_cc",
    ],
)

envoy_cc_library(
    name = "ratelimit_client_interface",
    hdrs = ["ratelimit.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/ratelimit:ratelimit_interface",
        "//include/envoy/singleton:manager_interface",
        "//include/envoy/tracing:http_tracer_interface",
    ],
)
