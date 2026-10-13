#
# This fishes the clang-tidy binary out of the related python package.
#
# This is useful as using the binary through the python entry_point adds a lot of overhead.
#
# ```starlark
#
# load("@base_pip3//:requirements.bzl", "requirement")
#
# clang_tidy(
#    name = "clang-tidy",
#    target = requirement("clang-tidy"),
# )
#
# ```
#
# The exposed binary can also be run directly:
#
# ```console
#
# $ bazel run //tools/clang-tidy -- --version
#
# ```
#

def _clang_tidy_impl(ctx):
    clang_bin = None
    for file in ctx.attr.target[DefaultInfo].data_runfiles.files.to_list():
        if file.basename == "clang-tidy" and file.dirname.split("/").pop() == "bin":
            clang_bin = file
            break

    if not clang_bin:
        fail("Unable to find clang-tidy file in package")

    output_file = ctx.actions.declare_file("clang-tidy")
    args = ctx.actions.args()
    args.add(clang_bin.path)
    args.add(output_file.path)
    ctx.actions.run(
        outputs = [output_file],
        inputs = [clang_bin],
        arguments = [args],
        executable = "cp",
        mnemonic = "ClangTidyGetter",
    )
    return [DefaultInfo(
        executable = output_file,
        files = depset([output_file]),
    )]

clang_tidy = rule(
    implementation = _clang_tidy_impl,
    attrs = {
        "target": attr.label(
            allow_files = True,
        ),
    },
    executable = True,
)
