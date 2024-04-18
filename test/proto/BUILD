load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_proto_descriptor",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

exports_files(["bookstore.proto"])

envoy_proto_library(
    name = "deprecated_proto",
    srcs = [":deprecated.proto"],
    deps = ["@envoy_api//envoy/annotations:pkg"],
)

envoy_proto_library(
    name = "helloworld_proto",
    srcs = [":helloworld.proto"],
)

envoy_proto_library(
    name = "apikeys_proto",
    srcs = [":apikeys.proto"],
)

envoy_proto_descriptor(
    name = "apikeys_proto_descriptor",
    srcs = [
        "apikeys.proto",
    ],
    out = "apikeys.descriptor",
    external_deps = [
        "api_httpbody_protos",
        "http_api_protos",
        "well_known_protos",
    ],
)

envoy_proto_library(
    name = "bookstore_proto",
    srcs = [":bookstore.proto"],
)

envoy_proto_descriptor(
    name = "bookstore_proto_descriptor",
    srcs = [
        "bookstore.proto",
        # JSON transcoder doesn't link against ":helloworld_proto_cc_proto", so we can add it to the
        # descriptor and test that we can actually transcode types not linked into the test binary.
        "helloworld.proto",
    ],
    out = "bookstore.descriptor",
    external_deps = [
        "api_httpbody_protos",
        "http_api_protos",
        "well_known_protos",
    ],
)

envoy_proto_library(
    name = "sensitive_proto",
    srcs = [":sensitive.proto"],
    deps = [
        "@com_github_cncf_xds//udpa/annotations:pkg",
        "@com_github_cncf_xds//xds/type/v3:pkg",
    ],
)
