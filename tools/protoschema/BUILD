load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package", "envoy_proto_library")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "dump_jsonschema",
    srcs = ["dump_jsonschema.py"],
    visibility = ["//visibility:public"],
    python_version = "PY3",
    srcs_version = "PY3",
    data = [
        "@com_envoyproxy_protoc_gen_validate//validate:validate_proto",
        "@com_github_chrusty_protoc_gen_jsonschema//:protoc-gen-jsonschema",
        "@com_github_cncf_udpa//udpa/annotations:pkg",
        "@com_google_googleapis//google/api:http_proto",
        "@com_google_googleapis//google/api:httpbody_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_googleapis//google/rpc:status_proto",
        "@com_google_protobuf//:protoc",
        "@com_google_protobuf//:well_known_protos",
        "@envoy_api_canonical//:v3_protos",
        "@opencensus_proto//opencensus/proto/trace/v1:trace_proto",
        "@prometheus_metrics_model//:client_model",
    ],
)


py_binary(
    name = "index_extensions",
    srcs = ["index_extensions.py"],
    visibility = ["//visibility:public"],
    python_version = "PY3",
    srcs_version = "PY3",
    data = [
        ":dump_jsonschema",
    ],
)


py_binary(
    name = "protoschema",
    srcs = ["protoschema.py"],
    visibility = ["//visibility:public"],
    python_version = "PY3",
    srcs_version = "PY3",
    data = [
        "//tools/extensions:generate_extension_db",
        ":index_extensions",
    ],
)
