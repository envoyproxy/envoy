load("@rules_proto//proto:defs.bzl", "ProtoInfo")

def create_protojsonschema_aspect(aspect_impl):
    _attrs = {
        "_protoc": attr.label(
            default = Label("@com_google_protobuf//:protoc"),
            executable = True,
            cfg = "exec",
        ),
        "_protoc_plugin": attr.label(
            default = Label("@com_github_chrusty_protoc_gen_jsonschema//cmd/protoc-gen-jsonschema:protoc-gen-jsonschema"),
            executable = True,
            cfg = "exec",
        ),
    }

    return aspect(
        attr_aspects = ["deps"],
        attrs = _attrs,
        implementation = aspect_impl,
    )

# Borrowed from https://github.com/grpc/grpc-java/blob/v1.24.1/java_grpc_library.bzl#L61
def _path_ignoring_repository(f):
    virtual_imports = "/_virtual_imports/"
    if virtual_imports in f.path:
        return f.path.split(virtual_imports)[1].split("/", 1)[1]
    elif len(f.owner.workspace_root) == 0:
        # |f| is in the main repository
        return f.short_path
    else:
        # If |f| is a generated file, it will have "bazel-out/*/genfiles" prefix
        # before "external/workspace", so we need to add the starting index of "external/workspace"
        return f.path[f.path.find(f.owner.workspace_root) + len(f.owner.workspace_root) + 1:]

def _protojsonschema_impl(target, ctx):
    output_group = "proto"

    transitive_outputs = depset(transitive = [dep.output_groups[output_group] for dep in ctx.rule.attr.deps])

    if ProtoInfo not in target:
        return [OutputGroupInfo(**{output_group: transitive_outputs})]

    proto_sources = [
        f
        for f in target[ProtoInfo].direct_sources
        if (f.path.startswith("external/envoy_api") or
            f.path.startswith("tools/testdata/protoxform/envoy") or
            f.path.startswith("external/com_github_cncf_udpa/xds"))
    ]

    if not proto_sources:
        return [OutputGroupInfo(**{output_group: transitive_outputs})]

    import_paths = []
    for f in target[ProtoInfo].transitive_sources.to_list():
        import_paths.append("{}={}".format(_path_ignoring_repository(f), f.path))

    # TODO: problem here => not 1 output per file and name can't be inferred
    # e.g. resource.proto => ResourceAnnotation.json
    outputs = []
    for f in proto_sources:
        f_path_split = f.path.split("/")
        output_file = ctx.label.name + "/" + f_path_split[len(f_path_split) - 1] + ".json"
        outputs.append(ctx.actions.declare_file(output_file))

    # Create the protoc command-line args.
    inputs = [target[ProtoInfo].transitive_sources]

    ctx_path = ctx.label.package + "/" + ctx.label.name
    output_path = outputs[0].root.path + "/" + outputs[0].owner.workspace_root + "/" + ctx_path

    args = ctx.actions.args()
    args.add(ctx.label.workspace_root, format = "-I./%s")
    args.add_all(import_paths, format_each = "-I%s")
    args.add(ctx.executable._protoc_plugin, format = "--plugin=protoc-gen-api_proto_plugin=%s")
    args.add(output_path, format = "--api_proto_plugin_out=%s")
    args.add_all(target[ProtoInfo].direct_sources)

    ctx.actions.run(
        inputs = depset(transitive = inputs),
        outputs = outputs,
        executable = ctx.executable._protoc,
        arguments = [args],
        tools = [ctx.executable._protoc_plugin],
    )

    transitive_outputs = depset(outputs, transitive = [transitive_outputs])
    return [OutputGroupInfo(**{output_group: transitive_outputs})]

protojsonschema_aspect = create_protojsonschema_aspect(_protojsonschema_impl)

def _protojsonschema_rule_impl(ctx):
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

protojsonschema_rule = rule(
    implementation = _protojsonschema_rule_impl,
    attrs = {
        "deps": attr.label_list(aspects = [protojsonschema_aspect]),
    },
)
