licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_proto_library",
    "envoy_proto_descriptor",
)

envoy_package()

exports_files(["bookstore.proto"])

# The binary proto descriptors bookstore_descriptor and bookstore_bad.descriptor are generated and
# stored prior to runtime. Should bookstore.proto change, the proto descriptors will need to be
# updated. The proto descriptors can be generated with bazel rule envoy_proto_descriptor. The
# generated proto descriptors can then be copied from bazel-genfiles/test/proto/ to test/proto/. For
# reference see: https://lyft.github.io/envoy/docs/configuration/http_filters/grpc_json_transcoder_
# filter.html#config-http-filters-grpc-json-transcoder.
#
# For example, to update bookstore.descriptor execute the following commands:
#   1. bazel build //test/proto:bookstore_proto_descriptor
#   2. cp bazel-genfiles/test/proto/bookstore.descriptor test/proto/
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
