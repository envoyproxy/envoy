load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "engine_builder_lib",
    srcs = [
        "engine_builder.cc",
    ],
    hdrs = [
        "engine_builder.h",
    ],
    repository = "@envoy",
    deps = [
        ":envoy_engine_cc_lib_no_stamp",
        "@envoy//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "envoy_engine_cc_lib_no_stamp",
    srcs = [
        "bridge_utility.cc",
        "engine.cc",
        "engine_callbacks.cc",
        "headers.cc",
        "headers_builder.cc",
        "key_value_store.cc",
        "log_level.cc",
        "request_headers.cc",
        "request_headers_builder.cc",
        "request_method.cc",
        "request_trailers.cc",
        "request_trailers_builder.cc",
        "response_headers.cc",
        "response_headers_builder.cc",
        "response_trailers.cc",
        "response_trailers_builder.cc",
        "retry_policy.cc",
        "stream.cc",
        "stream_callbacks.cc",
        "stream_client.cc",
        "stream_prototype.cc",
        "upstream_http_protocol.cc",
    ],
    hdrs = [
        "bridge_utility.h",
        "engine.h",
        "engine_callbacks.h",
        "envoy_error.h",
        "headers.h",
        "headers_builder.h",
        "key_value_store.h",
        "log_level.h",
        "pulse_client.h",
        "request_headers.h",
        "request_headers_builder.h",
        "request_method.h",
        "request_trailers.h",
        "request_trailers_builder.h",
        "response_headers.h",
        "response_headers_builder.h",
        "response_trailers.h",
        "response_trailers_builder.h",
        "retry_policy.h",
        "stream.h",
        "stream_callbacks.h",
        "stream_client.h",
        "stream_prototype.h",
        "trailers.h",
        "upstream_http_protocol.h",
    ],
    external_deps = ["abseil_optional"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        "//library/common:envoy_main_interface_lib_no_stamp",
        "//library/common/data:utility_lib",
        "//library/common/extensions/key_value/platform:config",
    ],
)

envoy_cc_library(
    name = "envoy_engine_cc_lib",
    repository = "@envoy",
    deps = [
        ":engine_builder_lib",
        ":envoy_engine_cc_lib_no_stamp",
        "//library/common:engine_common_lib_stamped",
    ],
)
