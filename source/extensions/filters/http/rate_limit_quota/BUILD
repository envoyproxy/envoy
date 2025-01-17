load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "rate_limit_quota",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":client_lib",
        ":matcher_lib",
        ":quota_bucket_cache",
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/registry",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/matcher:matcher_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//source/extensions/matching/input_matchers/cel_matcher:config",
        "@envoy_api//envoy/extensions/filters/http/rate_limit_quota/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/rate_limit_quota/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":global_client_lib",
        ":rate_limit_quota",
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/rate_limit_quota/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "client_lib",
    srcs = ["client_impl.cc"],
    hdrs = ["client_impl.h"],
    deps = [
        ":global_client_lib",
        ":quota_bucket_cache",
        "//envoy/grpc:async_client_interface",
        "//envoy/grpc:status",
        "//source/common/common:statusor_lib",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "global_client_lib",
    srcs = ["global_client_impl.cc"],
    hdrs = ["global_client_impl.h"],
    deps = [
        ":quota_bucket_cache",
        "//envoy/grpc:async_client_interface",
        "//envoy/grpc:status",
        "//source/common/common:statusor_lib",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/service/rate_limit_quota/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "matcher_lib",
    srcs = ["matcher.cc"],
    hdrs = ["matcher.h"],
    deps = [
        "//envoy/registry",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/matcher:matcher_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/rate_limit_quota/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/rate_limit_quota/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "quota_bucket_cache",
    hdrs = ["quota_bucket_cache.h"],
    deps = [
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/rate_limit_quota/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/rate_limit_quota/v3:pkg_cc_proto",
    ],
)
