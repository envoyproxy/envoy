load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "abac",
    srcs = ["abac.proto"],
    external_cc_proto_deps = [
        "@com_google_googleapis//google/api/expr/v1alpha1:syntax_cc_proto",
    ],
    external_proto_deps = [
        "@com_google_googleapis//google/api/expr/v1alpha1:syntax_proto",
    ],
    require_py = False,
)
