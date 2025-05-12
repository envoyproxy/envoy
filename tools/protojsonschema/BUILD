load("@rules_proto_grpc//:defs.bzl", "proto_plugin")
load(":generate.bzl", "jsonschema_compile")

licenses(["notice"])  # Apache 2

proto_plugin(
    name = "protoc_gen_jsonschema_proto_plugin",
    output_directory = True,
    tool = "@com_github_chrusty_protoc_gen_jsonschema//cmd/protoc-gen-jsonschema",
)

[
    jsonschema_compile(
        # example: "@envoy_api//envoy/config/bootstrap/v3:pkg" => "envoy_config_bootstrap_v3"
        name = proto.replace("@envoy_api//", "").replace("/", "_").replace(":pkg", ""),
        protos = [proto],
    )
    for proto in [
        "@envoy_api//envoy/config/bootstrap/v2:pkg",
        "@envoy_api//envoy/config/bootstrap/v3:pkg",
    ]
]
