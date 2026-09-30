load("//bazel:envoy_test.bzl", "envoy_cc_test", "envoy_test_env")

def _execution_context_transition_impl(settings, attr):
    if settings["//bazel:execution_context"]:
        return settings  # already enabled at top level; don't fork a redundant config
    return {"//bazel:execution_context": True}

execution_context_transition = transition(
    implementation = _execution_context_transition_impl,
    inputs = ["//bazel:execution_context"],
    outputs = ["//bazel:execution_context"],
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

    providers = [
        DefaultInfo(
            executable = out,
            default_runfiles = target[DefaultInfo].default_runfiles,
            data_runfiles = target[DefaultInfo].data_runfiles,
        ),
    ]

    # Forward coverage/instrumentation metadata if present
    if InstrumentedFilesInfo in target:
        providers.append(target[InstrumentedFilesInfo])

    # Forward other standard output groups if present
    if OutputGroupInfo in target:
        providers.append(target[OutputGroupInfo])

    return providers

_execution_context_enabled_test = rule(
    implementation = _execution_context_enabled_test_impl,
    test = True,
    executable = True,
    attrs = {
        "test": attr.label(
            mandatory = True,
            executable = True,
            cfg = execution_context_transition,
        ),
        "env": attr.string_dict(),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)

_ENGFLOW_RBE_X86_64 = Label("//bazel:engflow_rbe_x86_64")

def execution_context_enabled_test(name, test, rbe_pool = None, exec_properties = {}, args = [], env = {}, **kwargs):
    actual_exec_properties = exec_properties
    if rbe_pool:
        actual_exec_properties = actual_exec_properties | select({
            _ENGFLOW_RBE_X86_64: {"Pool": rbe_pool},
            "//conditions:default": {},
        })

    _execution_context_enabled_test(
        name = name,
        test = test,
        exec_properties = actual_exec_properties,
        args = args,
        env = envoy_test_env(env),
        **kwargs
    )

def execution_context_dual_test(name, **kwargs):
    # 1. Generate the base test target (with execution context disabled)
    envoy_cc_test(
        name = name,
        **kwargs
    )

    # 2. Extract standard test/executable attributes to pass to the enabled test
    enabled_kwargs = {}
    for attr in [
        "tags",
        "size",
        "timeout",
        "flaky",
        "shard_count",
        "local",
        "visibility",
        "deprecation",
        "features",
        "exec_properties",
        "rbe_pool",
        "args",
        "env",
    ]:
        if attr in kwargs:
            enabled_kwargs[attr] = kwargs[attr]

    # Append our custom tag to distinguish it
    enabled_tags = list(enabled_kwargs.get("tags", []))
    if "execution_context_enabled" not in enabled_tags:
        enabled_tags.append("execution_context_enabled")
    if "manual" not in enabled_tags:
        enabled_tags.append("manual")
    enabled_kwargs["tags"] = enabled_tags

    # Derive name for the enabled test target (e.g. "execution_context_test" -> "execution_context_enabled_test")
    enabled_name = name
    if enabled_name.endswith("_test"):
        enabled_name = enabled_name[:-5] + "_enabled_test"
    else:
        enabled_name = enabled_name + "_enabled"

    execution_context_enabled_test(
        name = enabled_name,
        test = ":" + name,
        **enabled_kwargs
    )
