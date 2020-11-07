load(":type_whisperer.bzl", "type_whisperer_aspect")

def _type_database_impl(ctx):
    type_db_deps = []
    for target in ctx.attr.targets:
        type_db_deps.append(target[OutputGroupInfo].types_pb_text)
    type_db_deps = depset(transitive = type_db_deps)

    args = [ctx.outputs.pb_text.path]
    for dep in type_db_deps.to_list():
        ws_name = dep.owner.workspace_name
        if (not ws_name) or ws_name in ctx.attr.proto_repositories:
            args.append(dep.path)

    ctx.actions.run(
        executable = ctx.executable._type_db_gen,
        arguments = args,
        inputs = type_db_deps,
        outputs = [ctx.outputs.pb_text],
        mnemonic = "TypeDbGen",
        use_default_shell_env = True,
    )

type_database = rule(
    attrs = {
        "targets": attr.label_list(
            aspects = [type_whisperer_aspect],
            doc = "List of all proto_library target to be included.",
        ),
        "proto_repositories": attr.string_list(
            default = ["envoy_api_canonical"],
            allow_empty = False,
        ),
        "_type_db_gen": attr.label(
            default = Label("//tools/type_whisperer:typedb_gen"),
            executable = True,
            cfg = "exec",
        ),
    },
    outputs = {
        "pb_text": "%{name}.pb_text",
    },
    implementation = _type_database_impl,
)
