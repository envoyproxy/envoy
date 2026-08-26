"""Protobuf toolchain helpers for consuming the registered prebuilt protoc.

The `//bazel/private:proto_toolchain_type` target itself has
`visibility = ["//visibility:public"]`; what is private and must not be
imported directly are the helper .bzl files under `//bazel/private/`.

Usage in an aspect or rule:

    load("//bazel:proto_toolchain.bzl", "use_proto_toolchain", "get_proto_compiler")

    my_aspect = aspect(
        toolchains = use_proto_toolchain(),
        ...
    )

    def _impl(target, ctx):
        protoc = get_proto_compiler(ctx)          # FilesToRunProvider
        ctx.actions.run(executable = protoc, ...)
"""

# The toolchain type registered via
#   native.register_toolchains(
#       "@protobuf//bazel/private/oss/toolchains/prebuilt:all")
# in bazel/toolchains.bzl.
_PROTO_TOOLCHAIN = "@protobuf//bazel/private:proto_toolchain_type"

def use_proto_toolchain():
    """Returns the toolchains list to declare on an aspect or rule."""
    return [_PROTO_TOOLCHAIN]

def get_proto_compiler(ctx):
    """Returns the FilesToRunProvider for protoc from the registered proto toolchain."""
    toolchain = ctx.toolchains[_PROTO_TOOLCHAIN]
    if not toolchain:
        fail("No proto toolchain registered for '%s'." % _PROTO_TOOLCHAIN)
    return toolchain.proto.proto_compiler
