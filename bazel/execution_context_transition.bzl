def _execution_context_transition_impl(settings, attr):
    # Append the compile flag to standard compiler options (copt)
    return {
        "//command_line_option:copt": settings["//command_line_option:copt"] + ["-DENVOY_ENABLE_EXECUTION_CONTEXT"],
    }

execution_context_transition = transition(
    implementation = _execution_context_transition_impl,
    inputs = ["//command_line_option:copt"],
    outputs = ["//command_line_option:copt"],
)

def _execution_context_enabled_test_impl(ctx):
    executable = ctx.executable.test
    target = ctx.attr.test[0]

    out = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.symlink(
        output = out,
        target_file = executable,
        is_executable = True,
    )

    return [
        DefaultInfo(
            executable = out,
            runfiles = target[DefaultInfo].default_runfiles,
        ),
    ]

execution_context_enabled_test = rule(
    implementation = _execution_context_enabled_test_impl,
    test = True,
    executable = True,
    attrs = {
        "test": attr.label(
            mandatory = True,
            executable = True,
            cfg = execution_context_transition,
        ),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)
