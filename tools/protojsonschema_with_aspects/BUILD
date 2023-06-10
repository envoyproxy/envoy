load("//tools/protojsonschema_with_aspects:protojsonschema.bzl", "protojsonschema_rule")

licenses(["notice"])  # Apache 2

protojsonschema_rule(
    name = "api_protojsonschema",
    visibility = ["//visibility:public"],
    deps = [
        #"@envoy_api//versioning:active_protos",
        #"@envoy_api//versioning:frozen_protos",
        "@envoy_api//envoy/annotations:pkg",  # just for testing
    ],
)
