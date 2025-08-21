#
# This fishes the clang-format binary out of the related python package.
#
# This is useful as using the binary through the python entry_point adds a lot of overhead.
#
# ```starlark
#
# load("@base_pip3//:requirements.bzl", "requirement")
#
# clang_format(
#    name = "clang-format",
#    target = requirement("clang-format"),
# )
#
# ```
#
# The exposed binary can also be run directly:
#
# ```console
#
# $ bazel run //tools/clang-format -- --version
#
# ```
#

def _clang_format_impl(ctx):
    clang_bin = None
    for file in ctx.attr.target[DefaultInfo].data_runfiles.files.to_list():
        if file.basename == "clang-format" and file.dirname.split("/").pop() == "bin":
            clang_bin = file
            break

    if not clang_bin:
        fail("Unable to find clang-format file in package")

    output_file = ctx.actions.declare_file("clang-format")
    args = ctx.actions.args()
    args.add(clang_bin.path)
    args.add(output_file.path)
    ctx.actions.run(
        outputs = [output_file],
        inputs = [clang_bin],
        arguments = [args],
        executable = "cp",
        mnemonic = "ClangFormatGetter",
    )
    return [DefaultInfo(
        executable = output_file,
        files = depset([output_file]),
    )]

clang_format = rule(
    implementation = _clang_format_impl,
    attrs = {
        "target": attr.label(
            allow_files = True,
        ),
    },
    executable = True,
)
