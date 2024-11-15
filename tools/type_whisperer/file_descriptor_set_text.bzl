load("@rules_proto//proto:defs.bzl", "ProtoInfo")

def _file_descriptor_set_text(ctx):
    file_descriptor_sets = depset(
        transitive = [dep[ProtoInfo].transitive_descriptor_sets for dep in ctx.attr.deps],
    )
    proto_repositories = ctx.attr.proto_repositories
    with_external_deps = ctx.attr.with_external_deps

    def _descriptor_set(dep):
        ws_name = dep.owner.workspace_name
        add_dep = (
            (not ws_name) or ws_name in proto_repositories or with_external_deps
        )
        if add_dep:
            return dep.path

    args = ctx.actions.args()
    args.add(ctx.outputs.pb_text)
    args.add_all(
        file_descriptor_sets,
        map_each = _descriptor_set,
        allow_closure = True,
    )

    ctx.actions.run(
        executable = ctx.executable._file_descriptor_set_text_gen,
        arguments = [args],
        inputs = file_descriptor_sets,
        outputs = [ctx.outputs.pb_text],
        mnemonic = "FileDescriptorSetTextGen",
        use_default_shell_env = True,
    )

file_descriptor_set_text = rule(
    attrs = {
        "deps": attr.label_list(
            doc = "List of all proto_library deps to be included.",
        ),
        "proto_repositories": attr.string_list(
            default = ["envoy_api"],
            allow_empty = False,
        ),
        "with_external_deps": attr.bool(
            doc = "Include file descriptors for external dependencies.",
            default = False,
        ),
        "_file_descriptor_set_text_gen": attr.label(
            default = Label("//tools/type_whisperer:file_descriptor_set_text_gen"),
            executable = True,
            cfg = "exec",
        ),
    },
    outputs = {
        "pb_text": "%{name}.pb_text",
    },
    implementation = _file_descriptor_set_text,
)
