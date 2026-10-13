load(
    "@rules_proto_grpc//:defs.bzl",
    "ProtoPluginInfo",
    "proto_compile_attrs",
    "proto_compile_impl",
)

# Create compile rule
jsonschema_compile = rule(
    implementation = proto_compile_impl,
    attrs = dict(
        proto_compile_attrs,
        _plugins = attr.label_list(
            providers = [ProtoPluginInfo],
            default = [
                Label(":protoc_gen_jsonschema_proto_plugin"),
            ],
            doc = "List of protoc plugins to apply",
        ),
    ),
    toolchains = [str(Label("@rules_proto_grpc//protobuf:toolchain_type"))],
)
