def _api_build_file(ctx):
    args = ctx.actions.args()
    args.add(ctx.file.typedb)
    args.add(ctx.outputs.build)
    ctx.actions.run(
        executable = ctx.executable._proto_build_targets_gen,
        arguments = [args],
        inputs = [ctx.file.typedb],
        outputs = [ctx.outputs.build],
        mnemonic = "ApiBuildFile",
        use_default_shell_env = True,
    )

api_build_file = rule(
    attrs = {
        "typedb": attr.label(
            doc = "Type database label.",
            mandatory = True,
            allow_single_file = True,
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
