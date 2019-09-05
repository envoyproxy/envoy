licenses(["notice"])  # Apache 2

load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

api_proto_package(
    deps = [
        "//envoy/api/v2/core",
        "//envoy/api/v2/route:pkg",
        "//envoy/type/matcher",
        "@com_google_googleapis//google/api/expr/v1alpha1:syntax_proto",
    ],
)

api_proto_library_internal(
    name = "rbac",
    srcs = ["rbac.proto"],
    external_cc_proto_deps = [
        "@com_google_googleapis//google/api/expr/v1alpha1:syntax_cc_proto",
    ],
    external_proto_deps = [
        "@com_google_googleapis//google/api/expr/v1alpha1:syntax_proto",
    ],
    external_py_proto_deps = [
        "@com_google_googleapis//google/api/expr/v1alpha1:syntax_py_proto",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v2/core:address",
        "//envoy/api/v2/route",
        "//envoy/type/matcher:metadata",
        "//envoy/type/matcher:string",
    ],
)
