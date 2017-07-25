licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_proto_library",
    "envoy_proto_descriptor",
)

envoy_package()

exports_files(["bookstore.proto"])

# The binary proto descriptors are generated and stored prior to runtime. The command to generate
# proto descriptors from the command line can be found here: https://lyft.github.io/envoy/docs/conf
# iguration/http_filters/grpc_json_transcoder_filter.html#config-http-filters-grpc-json-transcoder.
# Alternatively, the proto descriptors can be generated with bazel rule envoy_proto_descriptor. The
# generated proto descriptors can then be copied from bazel-genfiles/test/proto/... to
# test/proto/...
filegroup(
    name = "bookstore_descriptors",
    srcs = [
        "bookstore.descriptor",
        "bookstore_bad.descriptor",
    ],
)

envoy_proto_library(
    name = "helloworld_proto",
    srcs = [":helloworld.proto"],
)

envoy_proto_library(
    name = "bookstore_proto",
    srcs = [":bookstore.proto"],
    external_deps = [
        "http_api_protos",
        "cc_wkt_protos",
    ],
)

envoy_proto_descriptor(
    name = "bookstore_proto_descriptor",
    srcs = [
        "bookstore.proto",
    ],
    out = "bookstore.descriptor",
    external_deps = [
        "http_api_protos",
        "well_known_protos",
    ],
    protocopts = ["--include_imports"],
)

envoy_proto_descriptor(
    name = "bookstore_proto_descriptor_bad",
    srcs = [
        "bookstore.proto",
    ],
    out = "bookstore_bad.descriptor",
    external_deps = [
        "http_api_protos",
        "well_known_protos",
    ],
)
