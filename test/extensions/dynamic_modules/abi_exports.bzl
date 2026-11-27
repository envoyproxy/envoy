def _hidden_visibility_transition_impl(settings, attr):
    # Scoped to the dynamic modules TUs (plus the hickory outlier) so everything else in the
    # subgraph is cache-identical to the default configuration.
    return {
        "//command_line_option:per_file_copt": settings["//command_line_option:per_file_copt"] +
                                               ["dynamic_modules/.*[.](cc|cpp|cxx)$,hickory_dns_impl[.]cc$@-fvisibility=hidden"],
    }

_hidden_visibility_transition = transition(
    implementation = _hidden_visibility_transition_impl,
    inputs = ["//command_line_option:per_file_copt"],
    outputs = ["//command_line_option:per_file_copt"],
)

def _abi_exports_test_bin_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.symlink(output = out, target_file = ctx.executable.bin)
    return [DefaultInfo(files = depset([out]))]

abi_exports_test_bin = rule(
    implementation = _abi_exports_test_bin_impl,
    attrs = {
        "bin": attr.label(cfg = _hidden_visibility_transition, executable = True, mandatory = True),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)
