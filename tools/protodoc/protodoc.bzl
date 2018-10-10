# Borrowed from
# https://github.com/bazelbuild/rules_go/blob/master/proto/toolchain.bzl. This
# does some magic munging to remove workspace prefixes from output paths to
# convert path as understood by Bazel into paths as understood by protoc.
def _proto_path(proto):
    """
    The proto path is not really a file path
    It's the path to the proto that was seen when the descriptor file was generated.
    """
    path = proto.path
    root = proto.root.path
    ws = proto.owner.workspace_root
    if path.startswith(root):
        path = path[len(root):]
    if path.startswith("/"):
        path = path[1:]
    if path.startswith(ws):
        path = path[len(ws):]
    if path.startswith("/"):
        path = path[1:]
    return path

# Bazel aspect (https://docs.bazel.build/versions/master/skylark/aspects.html)
# that can be invoked from the CLI to produce docs via //tools/protodoc for
# proto_library targets. Example use:
#
#   bazel build //api --aspects tools/protodoc/protodoc.bzl%proto_doc_aspect \
#       --output_groups=rst
#
# The aspect builds the transitive docs, so any .proto in the dependency graph
# get docs created.
def _proto_doc_aspect_impl(target, ctx):
    # Compute RST files from the current proto_library node's dependencies.
    transitive_outputs = depset()
    for dep in ctx.rule.attr.deps:
        transitive_outputs = transitive_outputs | dep.output_groups["rst"]
    proto_sources = target.proto.direct_sources

    # If this proto_library doesn't actually name any sources, e.g. //api:api,
    # but just glues together other libs, we just need to follow the graph.
    if not proto_sources:
        return [OutputGroupInfo(rst = transitive_outputs)]

    # Figure out the set of import paths. Ideally we would use descriptor sets
    # built by proto_library, which avoid having to do nasty path mangling, but
    # these don't include source_code_info, which we need for comment
    # extractions. See https://github.com/bazelbuild/bazel/issues/3971.
    import_paths = []
    for f in target.proto.transitive_sources:
        if f.root.path:
            import_path = f.root.path + "/" + f.owner.workspace_root
        else:
            import_path = f.owner.workspace_root
        if import_path:
            import_paths += [import_path]

    # The outputs live in the ctx.label's package root. We add some additional
    # path information to match with protoc's notion of path relative locations.
    outputs = [ctx.actions.declare_file(ctx.label.name + "/" + _proto_path(f) +
                                        ".rst") for f in proto_sources]

    # Create the protoc command-line args.
    ctx_path = ctx.label.package + "/" + ctx.label.name
    output_path = outputs[0].root.path + "/" + outputs[0].owner.workspace_root + "/" + ctx_path
    args = ["-I./" + ctx.label.workspace_root]
    args += ["-I" + import_path for import_path in import_paths]
    args += ["--plugin=protoc-gen-protodoc=" + ctx.executable._protodoc.path, "--protodoc_out=" + output_path]
    args += [_proto_path(src) for src in target.proto.direct_sources]
    ctx.action(
        executable = ctx.executable._protoc,
        arguments = args,
        inputs = [ctx.executable._protodoc] + target.proto.transitive_sources.to_list(),
        outputs = outputs,
        mnemonic = "ProtoDoc",
        use_default_shell_env = True,
    )
    transitive_outputs = depset(outputs) | transitive_outputs
    return [OutputGroupInfo(rst = transitive_outputs)]

proto_doc_aspect = aspect(
    attr_aspects = ["deps"],
    attrs = {
        "_protoc": attr.label(
            default = Label("@com_google_protobuf//:protoc"),
            executable = True,
            cfg = "host",
        ),
        "_protodoc": attr.label(
            default = Label("//tools/protodoc"),
            executable = True,
            cfg = "host",
        ),
    },
    implementation = _proto_doc_aspect_impl,
)
