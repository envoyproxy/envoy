load(
    "@bazel_tools//tools/build_defs/cc:action_names.bzl",
    "CPP_COMPILE_ACTION_NAME",
)

def _pch(ctx):
    cc_toolchain = ctx.attr._cc_toolchain[cc_common.CcToolchainInfo]
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )

    cc_compiler_path = cc_common.get_tool_for_action(
        feature_configuration = feature_configuration,
        action_name = CPP_COMPILE_ACTION_NAME,
    )

    deps_cc_info = cc_common.merge_cc_infos(
        cc_infos = [dep[CcInfo] for dep in ctx.attr.deps],
    )

    # TODO: define somewhere to allow disables
    enable_pch = True
    if "clang" not in cc_compiler_path or not enable_pch:
        return [deps_cc_info]

    generated_header_file = ctx.actions.declare_file(ctx.label.name + ".h")
    ctx.actions.write(
        generated_header_file,
        "\n".join(["#include \"{}\"".format(include) for include in ctx.attr.includes]) + "\n",
    )

    pch_file = ctx.actions.declare_file(ctx.label.name + ".pch")

    # TODO: -fno-pch-timestamp / invalidation in that case doesn't work
    pch_flags = ["-x", "c++-header"]

    deps_ctx = deps_cc_info.compilation_context
    cc_compile_variables = cc_common.create_compile_variables(
        feature_configuration = feature_configuration,
        cc_toolchain = cc_toolchain,
        user_compile_flags = ctx.fragments.cpp.copts + ctx.fragments.cpp.cxxopts + pch_flags,
        source_file = generated_header_file.path,
        output_file = pch_file.path,
        preprocessor_defines = depset(deps_ctx.defines.to_list() + deps_ctx.local_defines.to_list()),
        include_directories = deps_ctx.includes,
        quote_include_directories = deps_ctx.quote_includes,
        system_include_directories = deps_ctx.system_includes,
        framework_include_directories = deps_ctx.framework_includes,
    )

    env = cc_common.get_environment_variables(
        feature_configuration = feature_configuration,
        action_name = CPP_COMPILE_ACTION_NAME,
        variables = cc_compile_variables,
    )

    command_line = cc_common.get_memory_inefficient_command_line(
        feature_configuration = feature_configuration,
        action_name = CPP_COMPILE_ACTION_NAME,
        variables = cc_compile_variables,
    )

    ctx.actions.run(
        executable = cc_compiler_path,
        arguments = command_line,
        env = env,
        inputs = depset(
            items = [generated_header_file],
            transitive = [cc_toolchain.all_files],
        ),
        outputs = [pch_file],
    )

    return [
        DefaultInfo(files = depset(items = [pch_file])),
        cc_common.merge_cc_infos(
            direct_cc_infos = [
                CcInfo(
                    compilation_context = cc_common.create_compilation_context(
                        includes = depset([pch_file.dirname]),
                        headers = depset([pch_file, generated_header_file]),
                    ),
                ),
            ],
            cc_infos = [deps_cc_info],
        ),
    ]

pch = rule(
    attrs = dict(
        includes = attr.string_list(
            mandatory = True,
            allow_empty = False,
        ),
        deps = attr.label_list(
            mandatory = True,
            allow_empty = False,
            providers = [CcInfo],
        ),
        _cc_toolchain = attr.label(default = Label("@bazel_tools//tools/cpp:current_cc_toolchain")),
    ),
    fragments = ["cpp"],
    provides = [CcInfo],
    toolchains = ["@bazel_tools//tools/cpp:toolchain_type"],
    implementation = _pch,
)
