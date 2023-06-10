load("@rules_proto//proto:defs.bzl", "ProtoInfo")
load("//tools/api_proto_plugin:plugin.bzl", "api_proto_plugin_aspect", "api_proto_plugin_impl")

def _protojsonschema_impl(target, ctx):
    return api_proto_plugin_impl(
        target, ctx,
        "proto", "protojsonschema",
        [], "jsonschema",
    )

protojsonschema_aspect = api_proto_plugin_aspect(
    "@com_github_chrusty_protoc_gen_jsonschema//cmd/protoc-gen-jsonschema",
    _protojsonschema_impl,
)

def _protojsonschema_rule_impl(ctx):
    deps = []
    for dep in ctx.attr.deps:
        for path in dep[OutputGroupInfo].proto.to_list():
            deps.append(path)

    return [
        DefaultInfo(
            files = depset(
                transitive = [
                    depset(deps),
                ],
            ),
        ),
    ]

protojsonschema_rule = rule(
    implementation = _protojsonschema_rule_impl,
    attrs = {
        "deps": attr.label_list(aspects = [protojsonschema_aspect]),
    },
)
