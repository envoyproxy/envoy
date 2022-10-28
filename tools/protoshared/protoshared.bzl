load("@envoy_api//bazel:api_build_system.bzl", "EnvoyProtoDepsInfo")

MNEMONIC = "ProtoShared"

def _protoshared_aspect_impl(target, ctx):
    descriptors = []
    for dep in ctx.rule.attr.deps:
        ws = dep.label.workspace_name
        if ws and ws != "envoy_api":
            continue
        for transitive in dep[ProtoInfo].transitive_descriptor_sets.to_list():
            if dep.label != transitive.owner:
                descriptors.append(transitive)
    return [OutputGroupInfo(transitive = descriptors)]

# Cycle through deps finding their deps and collecting descriptors for them.
protoshared_aspect = aspect(
    implementation = _protoshared_aspect_impl,
    attr_aspects = ["deps"],
)

def _protoshared_rule_impl(ctx):
    deps = {}
    transitive = []

    for dep in ctx.attr.deps:
        for t in dep[OutputGroupInfo].transitive.to_list():
            deps[str(t.owner)] = True
            transitive.append(t)
    return [
        OutputGroupInfo(transitive = depset(transitive)),
        EnvoyProtoDepsInfo(deps = deps.keys()),
    ]

protoshared_rule = rule(
    implementation = _protoshared_rule_impl,
    attrs = {
        "deps": attr.label_list(aspects = [protoshared_aspect]),
    },
)

def _protoshared_descriptor_set_impl(ctx):
    args = ctx.actions.args()
    transitive = []

    for source in ctx.attr.shared[OutputGroupInfo].transitive.to_list():
        ws_name = source.owner.workspace_name
        if ws_name and ws_name != "envoy_api":
            transitive.append(source)
        elif str(source.owner) in ctx.attr.shared[EnvoyProtoDepsInfo].deps:
            transitive.append(source)

    output = ctx.actions.declare_file("%s.pb" % ctx.attr.name)
    args.add(output)
    args.add_all(transitive)

    ctx.actions.run(
        executable = ctx.executable._file_concat,
        mnemonic = MNEMONIC,
        inputs = transitive,
        outputs = [output],
        arguments = [args],
    )

    return [
        DefaultInfo(
            files = depset([output]),
            runfiles = ctx.runfiles(files = [output]),
        ),
    ]

protoshared_descriptor_set = rule(
    implementation = _protoshared_descriptor_set_impl,
    attrs = {
        "shared": attr.label(providers = [EnvoyProtoDepsInfo]),
        "_file_concat": attr.label(
            default = "@rules_proto//tools/file_concat",
            executable = True,
            cfg = "exec",
        ),
    },
    doc = """
Collects all `FileDescriptorSet`s from the `deps` of `deps` and combines them
into a single `FileDescriptorSet` containing all the `FileDescriptorProto`.
""".strip(),
)
