"""Fast-fail on removed legacy --define flags."""

# define name -> replacement guidance
_REMOVED_DEFINES = {
    "wasm": "--@proxy-wasm-cpp-host//bazel:engine=<engine> (eg =disabled, =v8, =wasmtime)",
    "engine": "--@proxy-wasm-cpp-host//bazel:engine=<engine>",
}

def _legacy_define_check_impl(ctx):
    """Fail at analysis time if a removed legacy --define flag is set."""
    for name, replacement in _REMOVED_DEFINES.items():
        if name in ctx.var:
            fail(
                "`--define {name}={value}` is no longer supported and has no effect.\n".format(
                    name = name,
                    value = ctx.var[name],
                ) + "Use `{}` instead.".format(replacement),
            )
    return [DefaultInfo()]

legacy_define_check = rule(
    implementation = _legacy_define_check_impl,
    doc = "Fails analysis if a removed legacy --define is set.",
)
