load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    name = "matcher",
    deps = ["//envoy/type"],
)

api_proto_library_internal(
    name = "metadata",
    srcs = ["metadata.proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":value",
    ],
)

api_proto_library_internal(
    name = "number",
    srcs = ["number.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/type:range",
    ],
)

api_proto_library_internal(
    name = "string",
    srcs = ["string.proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":regex",
    ],
)

api_proto_library_internal(
    name = "value",
    srcs = ["value.proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":number",
        ":string",
    ],
)

api_proto_library_internal(
    name = "regex",
    srcs = ["regex.proto"],
    visibility = ["//visibility:public"],
)
