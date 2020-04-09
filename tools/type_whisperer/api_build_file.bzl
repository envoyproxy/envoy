def _api_build_file(ctx):
    pb_text_set = depset()
    type_db_file = ctx.attr.typedb.files.to_list()[0]
    args = [type_db_file.path, ctx.outputs.build.path]
    ctx.actions.run(
        executable = ctx.executable._proto_build_targets_gen,
        arguments = args,
        inputs = [type_db_file],
        outputs = [ctx.outputs.build],
        mnemonic = "ApiBuildFile",
        use_default_shell_env = True,
    )

api_build_file = rule(
    attrs = {
        "typedb": attr.label(
            doc = "Type database label.",
            mandatory = True,
        ),
        "_proto_build_targets_gen": attr.label(
            default = Label("//tools/type_whisperer:proto_build_targets_gen"),
            executable = True,
            cfg = "exec",
        ),
    },
    outputs = {
        "build": "BUILD.%{name}",
    },
    implementation = _api_build_file,
)
