load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")
load("@rules_proto//proto:defs.bzl", "ProtoInfo")

# Borrowed from https://github.com/grpc/grpc-java/blob/v1.24.1/java_grpc_library.bzl#L61
def _path_ignoring_repository(f):
    # Bazel creates a _virtual_imports directory in case the .proto source files
    # need to be accessed at a path that's different from their source path:
    # https://github.com/bazelbuild/bazel/blob/0.27.1/src/main/java/com/google/devtools/build/lib/rules/proto/ProtoCommon.java#L289
    #
    # In that case, the import path of the .proto file is the path relative to
    # the virtual imports directory of the rule in question.
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

def _input_arg(i):
    return "%s=%s" % (i.basename, i.path)

def api_proto_plugin_impl(target, ctx, output_group, mnemonic, output_suffixes):
    # Compute output files from the current proto_library node's dependencies.
    transitive_outputs = depset(transitive = [dep.output_groups[output_group] for dep in ctx.rule.attr.deps])

    # TODO(phlax): Return `ProtoInfo` from `proto` aspects, and this should
    #    not be necessary.
    if ProtoInfo not in target:
        return [OutputGroupInfo(**{output_group: transitive_outputs})]

    proto_sources = [
        f
        for f in target[ProtoInfo].direct_sources
        if (f.path.startswith("external/envoy_api") or
            f.path.startswith("tools/testdata/protoxform/envoy") or
            f.path.startswith("external/com_github_cncf_udpa/xds"))
    ]

    # If this proto_library doesn't actually name any sources, e.g. //api:api,
    # but just glues together other libs, we just need to follow the graph.
    if not proto_sources:
        return [OutputGroupInfo(**{output_group: transitive_outputs})]

    # Figure out the set of import paths. Ideally we would use descriptor sets
    # built by proto_library, which avoid having to do nasty path mangling, but
    # these don't include source_code_info, which we need for comment
    # extractions. See https://github.com/bazelbuild/bazel/issues/3971.
    import_paths = []
    for f in target[ProtoInfo].transitive_sources.to_list():
        import_paths.append("{}={}".format(_path_ignoring_repository(f), f.path))

    # The outputs live in the ctx.label's package root. We add some additional
    # path information to match with protoc's notion of path relative locations.
    outputs = []
    for output_suffix in output_suffixes:
        outputs += [ctx.actions.declare_file(ctx.label.name + "/" + _path_ignoring_repository(f) +
                                             output_suffix) for f in proto_sources]

    # Create the protoc command-line args.
    inputs = [target[ProtoInfo].transitive_sources]

    ctx_path = ctx.label.package + "/" + ctx.label.name
    output_path = outputs[0].root.path + "/" + outputs[0].owner.workspace_root + "/" + ctx_path
    args = ctx.actions.args()
    args.add(ctx.label.workspace_root, format = "-I./%s")
    args.add_all(import_paths, format_each = "-I%s")
    args.add(ctx.executable._api_proto_plugin, format = "--plugin=protoc-gen-api_proto_plugin=%s")
    args.add(output_path, format = "--api_proto_plugin_out=%s")
    if hasattr(ctx.attr, "_type_db"):
        inputs.append(ctx.attr._type_db.files)
        if len(ctx.attr._type_db.files.to_list()) != 1:
            fail("{} must have one type database file".format(ctx.attr._type_db))
        args.add(ctx.attr._type_db.files.to_list()[0], format = "--api_proto_plugin_opt=type_db_path=%s")
    if ctx.attr._extra_inputs:
        inputs.extend([i.files for i in ctx.attr._extra_inputs])
        args.add_all(ctx.files._extra_inputs, map_each = _input_arg, format_each = "--api_proto_plugin_opt=%s")
    if hasattr(ctx.attr, "_extra_args") and ctx.attr._extra_args[BuildSettingInfo].value:
        args.add(ctx.attr._extra_args[BuildSettingInfo].value, format = "--api_proto_plugin_opt=extra_args=%s")
    args.add_all(target[ProtoInfo].direct_sources)

    ctx.actions.run(
        executable = ctx.executable._protoc,
        arguments = [args],
        inputs = depset(transitive = inputs),
        tools = [ctx.executable._api_proto_plugin],
        outputs = outputs,
        mnemonic = mnemonic,
        use_default_shell_env = True,
    )

    transitive_outputs = depset(outputs, transitive = [transitive_outputs])
    return [OutputGroupInfo(**{output_group: transitive_outputs})]

def api_proto_plugin_aspect(
        tool_label,
        aspect_impl,
        use_type_db = False,
        extra_inputs = []):
    _attrs = {
        "_protoc": attr.label(
            default = Label("@com_google_protobuf//:protoc"),
            executable = True,
            cfg = "exec",
        ),
        "_api_proto_plugin": attr.label(
            default = Label(tool_label),
            executable = True,
            cfg = "exec",
        ),
        "_extra_inputs": attr.label_list(
            default = extra_inputs,
            allow_files = True,
        ),
    }
    if use_type_db:
        _attrs["_type_db"] = attr.label(
            default = Label("@envoy//tools/api_proto_plugin:default_type_db"),
        )
    _attrs["_extra_args"] = attr.label(
        default = Label("@envoy//tools/api_proto_plugin:extra_args"),
    )
    return aspect(
        attr_aspects = ["deps"],
        attrs = _attrs,
        implementation = aspect_impl,
    )
