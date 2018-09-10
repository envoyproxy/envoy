licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_proto_descriptor",
    "envoy_proto_library",
)

envoy_package()

exports_files(["bookstore.proto"])

envoy_proto_library(
    name = "helloworld_proto",
    srcs = [":helloworld.proto"],
)

envoy_proto_library(
    name = "bookstore_proto",
    srcs = [":bookstore.proto"],
    external_deps = ["api_httpbody_protos"],
)

envoy_proto_descriptor(
    name = "bookstore_proto_descriptor",
    srcs = [
        "bookstore.proto",
    ],
    out = "bookstore.descriptor",
    external_deps = [
        "api_httpbody_protos",
        "http_api_protos",
        "well_known_protos",
    ],
)
