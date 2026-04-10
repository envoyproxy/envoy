load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "types_lib",
    hdrs = ["types.h"],
    copts = [
        # Make sure that headers included from opentelemetry-api use Abseil from Envoy
        # https://github.com/open-telemetry/opentelemetry-cpp/blob/v1.14.0/api/BUILD#L32
        "-DHAVE_ABSEIL",
    ],
    deps = [
        "//source/extensions/common/opentelemetry/sdk/common:sdk_common_types_lib",
        "@opentelemetry-proto//:common_proto_cc",
        "@opentelemetry-proto//:trace_proto_cc",
    ],
)
