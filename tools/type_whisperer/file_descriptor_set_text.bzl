def _file_descriptor_set_text(ctx):
    file_descriptor_sets = depset()
    for dep in ctx.attr.deps:
        file_descriptor_sets = depset(transitive = [
            file_descriptor_sets,
            dep[ProtoInfo].transitive_descriptor_sets,
        ])

    args = [ctx.outputs.pb_text.path]
    for dep in file_descriptor_sets.to_list():
        ws_name = dep.owner.workspace_name
        if (not ws_name) or ws_name in ctx.attr.proto_repositories:
            args.append(dep.path)

    ctx.actions.run(
        executable = ctx.executable._file_descriptor_set_text_gen,
        arguments = args,
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
            default = ["envoy_api_canonical"],
            allow_empty = False,
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
