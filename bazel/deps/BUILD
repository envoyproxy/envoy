load("//bazel:envoy_build_system.bzl", "envoy_package")
load(":deps.bzl", "envoy_dep")

licenses(["notice"])  # Apache 2

envoy_package()

# TODO(envoyproxy/envoy#46786): Replace this migration-time visibility with generated allowlists
# from reachability data once #46761 is available.
_MIGRATION_VISIBILITY = ["//visibility:public"]

envoy_dep(
    name = "abseil_strings",
    actual = "@abseil-cpp//absl/strings",
    visibility = _MIGRATION_VISIBILITY,
)

envoy_dep(
    name = "grpc_transcoding",
    actual = "@grpc_httpjson_transcoding//src:transcoding",
    visibility = _MIGRATION_VISIBILITY,
    aliases = {
        "path_matcher": "@grpc_httpjson_transcoding//src:path_matcher",
    },
)

envoy_dep(
    name = "api_httpbody_protos",
    actual = "@googleapis//google/api:httpbody_cc_proto",
    visibility = _MIGRATION_VISIBILITY,
    aliases = {
        "http_api_protos": "@googleapis//google/api:annotations_cc_proto",
    },
)

envoy_dep(
    name = "nghttp2",
    actual = "@envoy//bazel/foreign_cc:nghttp2",
    visibility = _MIGRATION_VISIBILITY,
)

envoy_dep(
    name = "grpc",
    actual = "@grpc//:grpc++",
    visibility = _MIGRATION_VISIBILITY,
    aliases = {
        "grpc_health_proto": "@grpc//src/proto/grpc/health/v1:health_cc_proto",
    },
)

envoy_dep(
    name = "bazel_runfiles",
    actual = {
        "//bazel:windows_x86_64": "@bazel_tools//tools/cpp/runfiles",
        "//conditions:default": "@envoy//bazel:cpp_runfiles",
    },
    visibility = _MIGRATION_VISIBILITY,
)
