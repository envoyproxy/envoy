def _proto_cc_source(ctx):
    pb_text_set = depset()
    for src in ctx.attr.deps:
        pb_text_set = depset(transitive = [pb_text_set, src.files])
    args = [ctx.attr.constant, ctx.outputs.cc.path]
    for pb_text in pb_text_set.to_list():
        args.append(pb_text.path)
    ctx.actions.run(
        executable = ctx.executable._proto_cc_source_gen,
        arguments = args,
        inputs = pb_text_set,
        outputs = [ctx.outputs.cc],
        mnemonic = "ProtoCcSourceGen",
        use_default_shell_env = True,
    )

proto_cc_source = rule(
    attrs = {
        "constant": attr.string(
            doc = "Name of C++ constant definition.",
            mandatory = True,
        ),
        "deps": attr.label_list(
            doc = "List of all text protos to be included.",
        ),
        "proto_repositories": attr.string_list(
            default = ["envoy_api_canonical"],
            allow_empty = False,
        ),
        "_proto_cc_source_gen": attr.label(
            default = Label("//tools/type_whisperer:proto_cc_source_gen"),
            executable = True,
            cfg = "exec",
        ),
    },
    outputs = {
        "cc": "%{name}.cc",
    },
    implementation = _proto_cc_source,
)
