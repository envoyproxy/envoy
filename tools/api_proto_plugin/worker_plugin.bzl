load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")
load("@rules_proto//proto:defs.bzl", "ProtoInfo")
load("//tools/api_proto_plugin:plugin.bzl", "path_ignoring_repository")

def api_proto_worker_plugin_impl(target, ctx, output_group, mnemonic, output_suffixes):
    # Compute output files from the current proto_library node's dependencies.
    transitive_outputs = depset(transitive = [dep.output_groups[output_group] for dep in ctx.rule.attr.deps])
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

    # The outputs live in the ctx.label's package root. We add some additional
    # path information to match with protoc's notion of path relative locations.
    outputs = []
    for output_suffix in output_suffixes:
        outputs += [ctx.actions.declare_file(ctx.label.name + "/" + path_ignoring_repository(f) +
                                             output_suffix) for i, f in enumerate(proto_sources)]

    istest = False
    _infiles = []
    for f in proto_sources:
        if f.path.startswith("tools/testdata/protoxform"):
            istest = True
            _infiles.append(f.path)
        else:
            _infiles.append(f.path.split("/", 2)[2])

    infiles = ",".join(_infiles)

    # print(target)
    # print(infiles)

    args = ctx.actions.args()
    args.add("--in", infiles)
    args.add("--out", ",".join([p.path for p in outputs]))
    args.add("--suffixes", ",".join(output_suffixes))

    if hasattr(ctx.attr, "_type_db"):
        if len(ctx.attr._type_db.files.to_list()) != 1:
            fail("{} must have one type database file".format(ctx.attr._type_db))
        args.add("--type_db_path", ctx.attr._type_db.files.to_list()[0].path)
    if hasattr(ctx.attr, "_extra_args"):
        args.add("--extra_args=", ctx.attr._extra_args[BuildSettingInfo].value)

    worker_arg_file = ctx.actions.declare_file(proto_sources[0].path + "." + mnemonic + ".worker_args")

    # print(dir(proto_sources[0]))
    # print(proto_sources[0].path)
    # print("%s" % mnemonic)
    # print(args)

    ctx.actions.write(
        output = worker_arg_file,
        content = args,
    )

    plugin = (
        "%s.%s" %
        (
            ctx.attr._api_proto_plugin.label.package.replace("/", "."),
            ctx.attr._api_proto_plugin.label.name,
        )
    )
    arguments = [
        ctx.attr._protocol,
        ctx.attr._descriptor.files.to_list()[0].path,
        plugin,
        "@" + worker_arg_file.path,
    ]
    ctx.actions.run(
        executable = ctx.executable._worker,
        arguments = arguments,
        tools = ctx.attr._descriptor.files.to_list(),
        inputs = [worker_arg_file],
        outputs = outputs,
        mnemonic = mnemonic,
        use_default_shell_env = True,
        execution_requirements = {
            "supports-workers": "1",
            "requires-worker-protocol": "json",
        },
    )
    transitive_outputs = depset(outputs, transitive = [transitive_outputs])
    return [OutputGroupInfo(**{output_group: transitive_outputs})]

def api_proto_worker_plugin_aspect(
        tool_label,
        aspect_impl,
        worker,
        protocol = "envoy.base.utils.ProtocProtocol",
        descriptor = "@envoy_api//:v3_proto_set",
        type_db = None):
    _attrs = {
        "_worker": attr.label(
            default = Label(worker),
            executable = True,
            cfg = "exec",
        ),
        "_api_proto_plugin": attr.label(
            default = Label(tool_label),
        ),
        "_descriptor": attr.label(
            default = Label(descriptor),
        ),
        "_protocol": attr.string(
            default = protocol,
        ),
    }
    if type_db:
        _attrs["_type_db"] = attr.label(
            default = Label(type_db),
        )
    return aspect(
        attr_aspects = ["deps"],
        attrs = _attrs,
        implementation = aspect_impl,
    )
