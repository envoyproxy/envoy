"""Fallback for `--config=gcc` on platforms without a GCC toolchain."""

def _gcc_unavailable_impl(ctx):
    fail(
        "--config=gcc has no GCC toolchain for this platform. " +
        "Envoy's hermetic GCC builds Linux x86_64 targets on Linux x86_64 only.",
    )

gcc_unavailable = rule(implementation = _gcc_unavailable_impl)
