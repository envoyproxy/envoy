"""Rules for detecting and failing on deprecated build flags."""

def _check_removed_fips_define_impl(ctx):
    """Check if the deprecated --define boringssl=fips is set"""
    if ctx.var.get("boringssl") == "fips":
        fail("""
================================================================================
ERROR: --define boringssl=fips is deprecated and no longer supported.

Please use one of the new config options instead:

  For BoringSSL FIPS (Linux x86_64 only):
    bazel build --config=boringssl-fips //source/exe:envoy-static

See bazel/SSL.md for more details.
================================================================================
""")
    return []

check_removed_fips_define = rule(
    implementation = _check_removed_fips_define_impl,
    build_setting = config.string(flag = True),
)

# define name -> replacement guidance
_REMOVED_WASM_DEFINES = {
    "engine": "--@proxy-wasm-cpp-host//bazel:engine=<engine>",
    "wasm": "--@proxy-wasm-cpp-host//bazel:engine=<engine> (eg =disabled, =v8, =wasmtime)",
}

def _check_removed_wasm_defines_impl(ctx):
    """Check if the removed --define wasm/engine flags are set"""
    for name, replacement in _REMOVED_WASM_DEFINES.items():
        if name in ctx.var:
            fail("""
================================================================================
ERROR: --define {name}={value} is no longer supported and has no effect.

Please use the following instead:

  {replacement}

See bazel/README.md for more details.
================================================================================
""".format(name = name, replacement = replacement, value = ctx.var[name]))
    return []

check_removed_wasm_defines = rule(
    implementation = _check_removed_wasm_defines_impl,
    build_setting = config.string(flag = True),
)
