licenses(["notice"])  # Apache 2

proto_library(
    name = "simple_cache_proto",
    srcs = ["simple_cache.proto"],
    deps = [
        "@com_google_protobuf//:descriptor_proto",
    ],
)

cc_proto_library(
    name = "simple_cache_cc_proto",
    deps = [":simple_cache_proto"],
)

cc_library(
    name = "resp_cache",
    srcs = [
        # Add any .cc files if you have implementations in .cc files
    ],
    hdrs = [
        "resp_cache.h",
        "resp_cache_factory.h",
        "simple_cache.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":simple_cache_cc_proto",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/types:optional",
    ],
)
