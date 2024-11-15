load(":protoshared.bzl", "protoshared_descriptor_set", "protoshared_rule")

licenses(["notice"])  # Apache 2

protoshared_rule(
    name = "shared_protos",
    visibility = ["//visibility:public"],
    deps = [
        "@envoy_api//versioning:active_protos",
        "@envoy_api//versioning:frozen_protos",
    ],
)

protoshared_descriptor_set(
    name = "protoshared",
    shared = ":shared_protos",
    visibility = ["//visibility:public"],
)
