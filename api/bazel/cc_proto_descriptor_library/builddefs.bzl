"""Public rules for using cc proto descriptor library with protos:
  - cc_proto_descriptor_library()
"""

load("@bazel_skylib//lib:paths.bzl", "paths")

# begin:google_only
# load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain", "use_cpp_toolchain")
# end:google_only

# begin:github_only
# Compatibility code for Bazel 4.x. Remove this when we drop support for Bazel 4.x.
load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain")

def use_cpp_toolchain():
    return ["@bazel_tools//tools/cpp:toolchain_type"]

# end:github_only

# Generic support code #########################################################

# begin:github_only
_is_google3 = False
# end:github_only

# begin:google_only
# _is_google3 = True
# end:google_only

def _get_real_short_path(file):
    # For some reason, files from other archives have short paths that look like:
    #   ../com_google_protobuf/google/protobuf/descriptor.proto
    short_path = file.short_path
    if short_path.startswith("../"):
        second_slash = short_path.index("/", 3)
        short_path = short_path[second_slash + 1:]

    # Sometimes it has another few prefixes like:
    #   _virtual_imports/any_proto/google/protobuf/any.proto
    #   benchmarks/_virtual_imports/100_msgs_proto/benchmarks/100_msgs.proto
    # We want just google/protobuf/any.proto.
    virtual_imports = "_virtual_imports/"
    if virtual_imports in short_path:
        short_path = short_path.split(virtual_imports)[1].split("/", 1)[1]
    return short_path

def _get_real_root(ctx, file):
    real_short_path = _get_real_short_path(file)
    root = file.path[:-len(real_short_path) - 1]

    if not _is_google3 and ctx.rule.attr.strip_import_prefix:
        root = paths.join(root, ctx.rule.attr.strip_import_prefix[1:])
    return root

def _generate_output_file(ctx, src, extension):
    package = ctx.label.package
    if not _is_google3:
        strip_import_prefix = ctx.rule.attr.strip_import_prefix
        if strip_import_prefix and strip_import_prefix != "/":
            if not package.startswith(strip_import_prefix[1:]):
                fail("%s does not begin with prefix %s" % (package, strip_import_prefix))
            package = package[len(strip_import_prefix):]

    real_short_path = _get_real_short_path(src)
    real_short_path = paths.relativize(real_short_path, package)
    output_filename = paths.replace_extension(real_short_path, extension)
    ret = ctx.actions.declare_file(output_filename)
    return ret

def _generate_include_path(src, out, extension):
    short_path = _get_real_short_path(src)
    short_path = paths.replace_extension(short_path, extension)
    if not out.path.endswith(short_path):
        fail("%s does not end with %s" % (out.path, short_path))

    return out.path[:-len(short_path)]

def _filter_none(elems):
    out = []
    for elem in elems:
        if elem:
            out.append(elem)
    return out

def _cc_library_func(ctx, name, hdrs, srcs, copts, includes, dep_ccinfos):
    """Like cc_library(), but callable from rules.

    Args:
      ctx: Rule context.
      name: Unique name used to generate output files.
      hdrs: Public headers that can be #included from other rules.
      srcs: C/C++ source files.
      copts: Additional options for cc compilation.
      includes: Additional include paths.
      dep_ccinfos: CcInfo providers of dependencies we should build/link against.

    Returns:
      CcInfo provider for this compilation.
    """

    compilation_contexts = [info.compilation_context for info in dep_ccinfos]
    linking_contexts = [info.linking_context for info in dep_ccinfos]
    toolchain = find_cpp_toolchain(ctx)
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )

    blaze_only_args = {}

    if _is_google3:
        blaze_only_args["grep_includes"] = ctx.file._grep_includes

    (compilation_context, compilation_outputs) = cc_common.compile(
        actions = ctx.actions,
        feature_configuration = feature_configuration,
        cc_toolchain = toolchain,
        name = name,
        srcs = srcs,
        includes = includes,
        public_hdrs = hdrs,
        user_compile_flags = copts,
        compilation_contexts = compilation_contexts,
        **blaze_only_args
    )

    # buildifier: disable=unused-variable
    (linking_context, linking_outputs) = cc_common.create_linking_context_from_compilation_outputs(
        actions = ctx.actions,
        name = name,
        feature_configuration = feature_configuration,
        cc_toolchain = toolchain,
        compilation_outputs = compilation_outputs,
        linking_contexts = linking_contexts,
        disallow_dynamic_library = cc_common.is_enabled(feature_configuration = feature_configuration, feature_name = "targets_windows"),
        **blaze_only_args
    )

    return CcInfo(
        compilation_context = compilation_context,
        linking_context = linking_context,
    )

# Dummy rule to expose select() copts to aspects  ##############################

CcProtoDescriptorLibraryCoptsInfo = provider(
    "Provides copts for cc proto descriptor library targets",
    fields = {
        "copts": "copts for cc_proto_descriptor_library()",
    },
)

def cc_proto_descriptor_library_copts_impl(ctx):
    return CcProtoDescriptorLibraryCoptsInfo(copts = ctx.attr.copts)

cc_proto_descriptor_library_copts = rule(
    implementation = cc_proto_descriptor_library_copts_impl,
    attrs = {"copts": attr.string_list(default = [])},
)

# cc_proto_descriptor_library shared code #################

GeneratedSrcsInfo = provider(
    "Provides generated headers and sources",
    fields = {
        "srcs": "list of srcs",
        "hdrs": "list of hdrs",
        "includes": "list of extra includes",
    },
)

CcProtoDescriptorWrappedCcInfo = provider("Provider for cc_info for protos", fields = ["cc_info"])
_CcProtoDescriptorWrappedGeneratedSrcsInfo = provider("Provider for generated sources", fields = ["srcs"])

def _compile_protos(ctx, generator, proto_info, proto_sources):
    if len(proto_sources) == 0:
        return GeneratedSrcsInfo(srcs = [], hdrs = [], includes = [])

    ext = "_" + generator
    tool = getattr(ctx.executable, "_gen_" + generator)
    srcs = [_generate_output_file(ctx, name, ext + ".pb.cc") for name in proto_sources]
    hdrs = [_generate_output_file(ctx, name, ext + ".pb.h") for name in proto_sources]
    transitive_sets = proto_info.transitive_descriptor_sets.to_list()

    args = ctx.actions.args()
    args.use_param_file(param_file_arg = "@%s")
    args.set_param_file_format("multiline")

    args.add("--" + generator + "_out=" + _get_real_root(ctx, srcs[0]))
    args.add("--plugin=protoc-gen-" + generator + "=" + tool.path)
    args.add("--descriptor_set_in=" + ctx.configuration.host_path_separator.join([f.path for f in transitive_sets]))
    args.add_all(proto_sources, map_each = _get_real_short_path)

    ctx.actions.run(
        inputs = depset(
            direct = [proto_info.direct_descriptor_set],
            transitive = [proto_info.transitive_descriptor_sets],
        ),
        tools = [tool],
        outputs = srcs + hdrs,
        executable = ctx.executable._protoc,
        arguments = [args],
        progress_message = "Generating descriptor protos for :" + ctx.label.name,
        mnemonic = "GenDescriptorProtos",
    )
    return GeneratedSrcsInfo(
        srcs = srcs,
        hdrs = hdrs,
        includes = [_generate_include_path(proto_sources[0], hdrs[0], "_descriptor.pb.h")],
    )

def _cc_proto_descriptor_rule_impl(ctx):
    if len(ctx.attr.deps) != 1:
        fail("only one deps dependency allowed.")
    dep = ctx.attr.deps[0]

    if _CcProtoDescriptorWrappedGeneratedSrcsInfo in dep:
        srcs = dep[_CcProtoDescriptorWrappedGeneratedSrcsInfo].srcs
    else:
        fail("proto_library rule must generate _CcProtoDescriptorWrappedGeneratedSrcsInfo " +
             "(aspect should have handled this).")

    if CcProtoDescriptorWrappedCcInfo in dep:
        cc_info = dep[CcProtoDescriptorWrappedCcInfo].cc_info
    else:
        fail("proto_library rule must generate CcProtoDescriptorWrappedCcInfo  " +
             "(aspect should have handled this).")

    lib = cc_info.linking_context.linker_inputs.to_list()[0].libraries[0]
    files = _filter_none([
        lib.static_library,
        lib.pic_static_library,
        lib.dynamic_library,
    ])
    return [
        DefaultInfo(files = depset(files + srcs.hdrs + srcs.srcs)),
        srcs,
        cc_info,
    ]

def _cc_proto_descriptor_aspect_impl(target, ctx, generator, cc_provider, file_provider, provide_cc_shared_library_hints = True):
    proto_info = target[ProtoInfo]
    files = _compile_protos(ctx, generator, proto_info, proto_info.direct_sources)
    deps = ctx.rule.attr.deps + getattr(ctx.attr, "_" + generator)
    dep_ccinfos = [dep[CcInfo] for dep in deps if CcInfo in dep]
    dep_ccinfos += [dep[CcProtoDescriptorWrappedCcInfo].cc_info for dep in deps if CcProtoDescriptorWrappedCcInfo in dep]
    name = ctx.rule.attr.name + "." + generator
    owners = [ctx.label.relative(name)]
    cc_info = _cc_library_func(
        ctx = ctx,
        name = name,
        hdrs = files.hdrs,
        srcs = files.srcs,
        includes = files.includes,
        #copts = ctx.attr._copts[CcProtoDescriptorLibraryCoptsInfo].copts,
        copts = [],
        dep_ccinfos = dep_ccinfos,
    )

    wrapped_cc_info = cc_provider(
        cc_info = cc_info,
    )
    providers = [
        wrapped_cc_info,
        file_provider(srcs = files),
    ]
    if provide_cc_shared_library_hints:
        if hasattr(cc_common, "CcSharedLibraryHintInfo"):
            providers.append(cc_common.CcSharedLibraryHintInfo(owners = owners))
        elif hasattr(cc_common, "CcSharedLibraryHintInfo_6_X_constructor_do_not_use"):
            # This branch can be deleted once 6.X is not supported by rules
            providers.append(cc_common.CcSharedLibraryHintInfo_6_X_constructor_do_not_use(owners = owners))
    return providers

def cc_proto_descriptor_library_aspect_impl(target, ctx):
    return _cc_proto_descriptor_aspect_impl(target, ctx, "descriptor", CcProtoDescriptorWrappedCcInfo, _CcProtoDescriptorWrappedGeneratedSrcsInfo)

def _maybe_add(d):
    if _is_google3:
        d["_grep_includes"] = attr.label(
            allow_single_file = True,
            cfg = "exec",
            default = "@bazel_tools//tools/cpp:grep-includes",
        )
    return d

# cc_proto_descriptor_library() ##########################################################

def _get_cc_proto_descriptor_library_aspect_provides():
    provides = [
        CcProtoDescriptorWrappedCcInfo,
        _CcProtoDescriptorWrappedGeneratedSrcsInfo,
    ]

    if hasattr(cc_common, "CcSharedLibraryHintInfo"):
        provides.append(cc_common.CcSharedLibraryHintInfo)
    elif hasattr(cc_common, "CcSharedLibraryHintInfo_6_X_getter_do_not_use"):
        # This branch can be deleted once 6.X is not supported by upb rules
        provides.append(cc_common.CcSharedLibraryHintInfo_6_X_getter_do_not_use)

    return provides

cc_proto_descriptor_library_aspect = aspect(
    attrs = _maybe_add({
        #"_copts": attr.label(
        #    default = "//:upb_proto_library_copts__for_generated_code_only_do_not_use",
        #),
        "_gen_descriptor": attr.label(
            executable = True,
            cfg = "exec",
            default = "//bazel/cc_proto_descriptor_library:file_descriptor_generator",
        ),
        "_protoc": attr.label(
            executable = True,
            cfg = "exec",
            default = "@com_google_protobuf//:protoc",
        ),
        "_cc_toolchain": attr.label(
            default = "@bazel_tools//tools/cpp:current_cc_toolchain",
        ),
        "_descriptor": attr.label_list(
            default = [
                Label("//bazel/cc_proto_descriptor_library:file_descriptor_info"),
                Label("@com_google_absl//absl/base:core_headers"),
            ],
        ),
    }),
    implementation = cc_proto_descriptor_library_aspect_impl,
    provides = _get_cc_proto_descriptor_library_aspect_provides(),
    attr_aspects = ["deps"],
    fragments = ["cpp"],
    toolchains = use_cpp_toolchain(),
)

cc_proto_descriptor_library = rule(
    implementation = _cc_proto_descriptor_rule_impl,
    attrs = {
        "deps": attr.label_list(
            aspects = [cc_proto_descriptor_library_aspect],
            allow_rules = ["proto_library"],
            providers = [ProtoInfo],
        ),
    },
    provides = [CcInfo],
)
