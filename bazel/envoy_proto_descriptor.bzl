# A `ProtoInfo`-based rule for generating a `FileDescriptorSet` from a set of
# `proto_library` targets. `ProtoInfo.transitive_proto_path` and
# `ProtoInfo.transitive_sources` provide the correct `protoc` include paths and
# inputs for free, under both bzlmod and `WORKSPACE` repository layouts.
load("@envoy_toolshed//toolchains:utils.bzl", "get_proto_compiler", "use_proto_toolchain")
load("@protobuf//bazel/common:proto_info.bzl", "ProtoInfo")

PROTO_TOOLCHAIN_TYPE = Label("@protobuf//bazel/private:proto_toolchain_type")

def _envoy_proto_descriptor_impl(ctx):
    output = ctx.outputs.out
    transitive_sources = depset(transitive = [
        dep[ProtoInfo].transitive_sources
        for dep in ctx.attr.deps
    ])
    include_paths = depset(transitive = [
        dep[ProtoInfo].transitive_proto_path
        for dep in ctx.attr.deps
    ])

    args = ctx.actions.args()
    args.add("--include_imports")
    args.add_all(include_paths, format_each = "-I%s")
    args.add(output, format = "--descriptor_set_out=%s")
    args.add_all(transitive_sources)

    ctx.actions.run(
        executable = get_proto_compiler(ctx, PROTO_TOOLCHAIN_TYPE),
        arguments = [args],
        inputs = transitive_sources,
        outputs = [output],
        mnemonic = "EnvoyProtoDescriptor",
    )
    return [DefaultInfo(files = depset([output]))]

envoy_proto_descriptor_rule = rule(
    implementation = _envoy_proto_descriptor_impl,
    attrs = {
        "deps": attr.label_list(providers = [ProtoInfo]),
        "out": attr.output(mandatory = True),
    },
    toolchains = use_proto_toolchain(PROTO_TOOLCHAIN_TYPE),
)
