load("//tools/api_proto_plugin:plugin.bzl", "api_proto_plugin_aspect", "api_proto_plugin_impl")

def _protoxform_impl(target, ctx):
    return api_proto_plugin_impl(
        target,
        ctx,
        "proto",
        "protoxform",
        [".active_or_frozen.proto"],
    )

# Bazel aspect (https://docs.bazel.build/versions/master/starlark/aspects.html)
# that can be invoked from the CLI to perform API transforms via //tools/protoxform for
# proto_library targets. Example use:
#
#   bazel build //api --aspects tools/protoxform/protoxform.bzl%protoxform_aspect \
#       --output_groups=proto
#
protoxform_aspect = api_proto_plugin_aspect("//tools/protoxform", _protoxform_impl, use_type_db = True)

def _protoxform_rule_impl(ctx):
    deps = []
    for dep in ctx.attr.deps:
        for path in dep[OutputGroupInfo].proto.to_list():
            envoy_api = (
                path.short_path.startswith("../envoy_api") or
                path.short_path.startswith("../com_github_cncf_udpa") or
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

protoxform_rule = rule(
    implementation = _protoxform_rule_impl,
    attrs = {
        "deps": attr.label_list(aspects = [protoxform_aspect]),
    },
)
