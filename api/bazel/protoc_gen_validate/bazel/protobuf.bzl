load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")
load("@bazel_tools//tools/jdk:toolchain_utils.bzl", "find_java_runtime_toolchain", "find_java_toolchain")
load("@com_google_protobuf//bazel/common:proto_info.bzl", "ProtoInfo")

# Borrowed from https://github.com/grpc/grpc-java/blob/v1.28.0/java_grpc_library.bzl#L59
# "repository" here is for Bazel builds that span multiple WORKSPACES.
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

def _protoc_cc_output_file(ctx, proto_file):
    file_path = proto_file.basename

    if proto_file.basename.endswith(".proto"):
        file_path = file_path[:-len(".proto")]

    return (file_path + ".pb.validate.h", file_path + ".pb.validate.cc")

def _proto_sources(ctx):
    protos = []
    for dep in ctx.attr.deps:
        protos += [f for f in dep[ProtoInfo].direct_sources]

    return protos

def _output_dir(ctx):
    dir_out = ctx.genfiles_dir.path
    if ctx.label.workspace_root:
        dir_out += "/" + ctx.label.workspace_root
    return dir_out

def _protoc_gen_validate_cc_impl(ctx):
    """Generate C++ protos using protoc-gen-validate plugin"""
    flavor = ctx.attr._flavor[BuildSettingInfo].value or ""
    protos = _proto_sources(ctx)
    out_files = []

    for f in protos:
        for out in _protoc_cc_output_file(ctx, f):
            out_files.append(ctx.actions.declare_file(out, sibling = f))

    dir_out = _output_dir(ctx)

    args = [
        "--validate_out=lang=cc" + flavor + ":" + dir_out,
    ]

    return _protoc_gen_validate_impl(
        ctx = ctx,
        lang = "cc",
        protos = protos,
        out_files = out_files,
        protoc_args = args,
        package_command = "true",
    )

def _protoc_gen_validate_impl(ctx, lang, protos, out_files, protoc_args, package_command):
    protoc_args.append("--plugin=protoc-gen-validate=" + ctx.executable._plugin.path)

    dir_out = ctx.genfiles_dir.path
    if ctx.label.workspace_root:
        dir_out += "/" + ctx.label.workspace_root

    tds = depset([], transitive = [dep[ProtoInfo].transitive_descriptor_sets for dep in ctx.attr.deps])
    descriptor_args = [ds.path for ds in tds.to_list()]

    if len(descriptor_args) != 0:
        protoc_args.append("--descriptor_set_in=%s" % ctx.configuration.host_path_separator.join(descriptor_args))

    package_command = package_command.format(dir_out = dir_out)

    ctx.actions.run_shell(
        outputs = out_files,
        inputs = protos + tds.to_list(),
        tools = [ctx.executable._plugin, ctx.executable._protoc],
        command = " && ".join([
            ctx.executable._protoc.path + " $@",
            package_command,
        ]),
        arguments = protoc_args + [_path_ignoring_repository(proto) for proto in protos],
        mnemonic = "ProtoGenValidate" + lang.capitalize() + "Generate",
        use_default_shell_env = True,
    )

    return DefaultInfo(
        files = depset(out_files),
    )

cc_proto_gen_validate = rule(
    attrs = {
        "deps": attr.label_list(
            mandatory = True,
            providers = [ProtoInfo],
        ),
        "_validate_deps": attr.label_list(
            default = [Label("@re2")],
        ),
        "_protoc": attr.label(
            cfg = "exec",
            default = Label("@com_google_protobuf//:protoc"),
            executable = True,
            allow_single_file = True,
        ),
        "_plugin": attr.label(
            cfg = "exec",
            default = Label("@com_envoyproxy_protoc_gen_validate//:protoc-gen-validate"),
            allow_files = True,
            executable = True,
        ),
        "_flavor": attr.label(default = ":template-flavor"),
    },
    output_to_genfiles = True,
    implementation = _protoc_gen_validate_cc_impl,
)

_ProtoValidateSourceInfo = provider(
    fields = {
        "sources": "Depset of sources created by protoc with protoc-gen-validate plugin",
    },
)

def _create_include_path(include):
    return "--proto_path={0}={1}".format(_path_ignoring_repository(include), include.path)

def _java_proto_gen_validate_aspect_impl(target, ctx):
    proto_info = target[ProtoInfo]
    includes = proto_info.transitive_imports
    srcs = proto_info.direct_sources
    options = ",".join(["lang=java"])
    srcjar = ctx.actions.declare_file("%s-validate-gensrc.jar" % ctx.label.name)

    args = ctx.actions.args()
    args.add(ctx.executable._plugin.path, format = "--plugin=protoc-gen-validate=%s")
    args.add("--validate_out={0}:{1}".format(options, srcjar.path))
    args.add_all(includes, map_each = _create_include_path)
    args.add_all(srcs, map_each = _path_ignoring_repository)

    ctx.actions.run(
        inputs = depset(transitive = [proto_info.transitive_imports]),
        outputs = [srcjar],
        executable = ctx.executable._protoc,
        arguments = [args],
        tools = [ctx.executable._plugin],
        progress_message = "Generating %s" % srcjar.path,
    )

    return [_ProtoValidateSourceInfo(
        sources = depset(
            [srcjar],
            transitive = [dep[_ProtoValidateSourceInfo].sources for dep in ctx.rule.attr.deps],
        ),
    )]

_java_proto_gen_validate_aspect = aspect(
    _java_proto_gen_validate_aspect_impl,
    provides = [_ProtoValidateSourceInfo],
    attr_aspects = ["deps"],
    attrs = {
        "_protoc": attr.label(
            cfg = "exec",
            default = Label("@com_google_protobuf//:protoc"),
            executable = True,
            allow_single_file = True,
        ),
        "_plugin": attr.label(
            cfg = "exec",
            default = Label("@com_envoyproxy_protoc_gen_validate//:protoc-gen-validate"),
            allow_files = True,
            executable = True,
        ),
    },
)

def _java_proto_gen_validate_impl(ctx):
    source_jars = [source_jar for dep in ctx.attr.deps for source_jar in dep[_ProtoValidateSourceInfo].sources.to_list()]

    deps = [java_common.make_non_strict(dep[JavaInfo]) for dep in ctx.attr.java_deps]
    deps += [dep[JavaInfo] for dep in ctx.attr._validate_deps]

    java_info = java_common.compile(
        ctx,
        source_jars = source_jars,
        deps = deps,
        output_source_jar = ctx.outputs.srcjar,
        output = ctx.outputs.jar,
        java_toolchain = find_java_toolchain(ctx, ctx.attr._java_toolchain),
    )

    return [java_info]

"""Bazel rule to create a Java protobuf validation library from proto sources files.

Args:
  deps: proto_library rules that contain the necessary .proto files
  java_deps: the java_proto_library of the protos being compiled.
"""
java_proto_gen_validate = rule(
    attrs = {
        "deps": attr.label_list(
            providers = [ProtoInfo],
            aspects = [_java_proto_gen_validate_aspect],
            mandatory = True,
        ),
        "java_deps": attr.label_list(
            providers = [JavaInfo],
            mandatory = True,
        ),
        "_validate_deps": attr.label_list(
            default = [
                Label("@com_envoyproxy_protoc_gen_validate//validate:validate_java"),
                Label("@com_google_re2j//jar"),
                Label("@com_google_protobuf//:protobuf_java"),
                Label("@com_google_protobuf//:protobuf_java_util"),
                Label("@com_envoyproxy_protoc_gen_validate//java/pgv-java-stub/src/main/java/io/envoyproxy/pgv"),
                Label("@com_envoyproxy_protoc_gen_validate//java/pgv-java-validation/src/main/java/io/envoyproxy/pgv"),
            ],
        ),
        "_java_toolchain": attr.label(default = Label("@bazel_tools//tools/jdk:current_java_toolchain")),
    },
    fragments = ["java"],
    provides = [JavaInfo],
    outputs = {
        "jar": "lib%{name}.jar",
        "srcjar": "lib%{name}-src.jar",
    },
    implementation = _java_proto_gen_validate_impl,
    toolchains = ["@bazel_tools//tools/jdk:toolchain_type"],
)
