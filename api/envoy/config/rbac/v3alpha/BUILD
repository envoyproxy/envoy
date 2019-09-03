licenses(["notice"])  # Apache 2

load("@envoy_api//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library_internal")

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
        "//envoy/api/v3alpha/core:address",
        "//envoy/api/v3alpha/route",
        "//envoy/type/matcher:metadata",
        "//envoy/type/matcher:string",
    ],
)

api_go_proto_library(
    name = "rbac",
    proto = ":rbac",
    deps = [
        "//envoy/api/v3alpha/core:address_go_proto",
        "//envoy/api/v3alpha/route:route_go_proto",
        "//envoy/type/matcher:metadata_go_proto",
        "//envoy/type/matcher:string_go_proto",
        "@com_google_googleapis//google/api/expr/v1alpha1:cel_go_proto",
    ],
)
