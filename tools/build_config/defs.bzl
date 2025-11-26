load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")

def _toolchain_enforcement_impl(ctx):
    """Implementation of the toolchain enforcement rule.
    
    This rule checks if a C++ toolchain has been explicitly selected.
    If not, it fails with a helpful error message.
    """
    toolchain_id = ctx.attr.toolchain_identifier[BuildSettingInfo].value
    
    if toolchain_id == "":
        fail("""
╔═══════════════════════════════════════════════════════════════════════════════╗
║                   C++ Toolchain Selection Required                            ║
╚═══════════════════════════════════════════════════════════════════════════════╝

ERROR: No C++ toolchain has been selected for this build.

Automatic C++ toolchain detection is disabled in this repository.
You must explicitly specify a toolchain using one of the following options:

  • For GCC:   bazel build --config=gcc <target>
  • For Clang: bazel build --config=clang <target>

You can also set a default in your user.bazelrc file:
  build --config=gcc

For more information, see the Envoy developer documentation.
""")
    
    # Return an empty default info provider - this rule doesn't produce any outputs
    return [DefaultInfo()]

toolchain_enforcement = rule(
    implementation = _toolchain_enforcement_impl,
    attrs = {
        "toolchain_identifier": attr.label(
            providers = [BuildSettingInfo],
            mandatory = True,
            doc = "The toolchain_identifier build setting to check",
        ),
    },
    doc = """
    Rule that enforces explicit C++ toolchain selection.
    
    This rule reads the toolchain_identifier build setting and fails the build
    if no toolchain has been explicitly selected (i.e., the value is empty).
    """,
)
