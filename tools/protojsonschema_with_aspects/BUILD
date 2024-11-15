load("//tools/protojsonschema_with_aspects:protojsonschema.bzl", "protojsonschema_rule")

licenses(["notice"])  # Apache 2

protojsonschema_rule(
    name = "api_protojsonschema",
    visibility = ["//visibility:public"],
    deps = [
        "@envoy_api//envoy/config/bootstrap/v2:pkg",
        "@envoy_api//envoy/config/bootstrap/v3:pkg",
    ],
)
