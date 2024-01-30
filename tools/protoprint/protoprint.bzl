load("//tools/api_proto_plugin:plugin.bzl", "api_proto_plugin_aspect", "api_proto_plugin_impl")

def _protoprint_impl(target, ctx):
    return api_proto_plugin_impl(
        target,
        ctx,
        "pproto",
        "protoprint",
        [".proto"],
    )

# Bazel aspect (https://docs.bazel.build/versions/master/starlark/aspects.html)
# that can be invoked from the CLI to perform API transforms via //tools/protoprint for
# proto_library targets. Example use:
#
#   bazel build //api --aspects tools/protoprint/protoprint.bzl%protoprint_aspect \
#       --output_groups=proto
#
protoprint_aspect = api_proto_plugin_aspect(
    "//tools/protoprint",
    _protoprint_impl,
    use_type_db = True,
    extra_inputs = ["//:.clang-format", "//tools/clang-format"],
)

def _protoprint_rule_impl(ctx):
    deps = []
    for dep in ctx.attr.deps:
        for path in dep[OutputGroupInfo].pproto.to_list():
            envoy_api = (
                path.short_path.startswith("../envoy_api") or
                path.short_path.startswith("tools/testdata")
            )
            if envoy_api:
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

protoprint_rule = rule(
    implementation = _protoprint_rule_impl,
    attrs = {
        "deps": attr.label_list(aspects = [protoprint_aspect]),
    },
)
