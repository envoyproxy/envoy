#
# This fishes the zstd binary out of the foreign_cc build.
#
# This is useful as zstd can run multicore when compressing.
#
# ```starlark
#
# zstd(
#    name = "zstd",
#    target = "//bazel/foreign_cc:zstd",
# )
#
# pkg_tar(
#     name = "compressed_foo",
#     extension = "tar.zst",
#     srcs = [":foos"],
#     compressor = "//tools/zstd",
#     compressor_args = "-T0",
# )
#
# ```
#
# The exposed binary can also be run directly:
#
# ```console
#
# $ bazel run //tools/zstd -- --version
#
# ```
#

def _zstd_impl(ctx):
    generated_dir = ctx.attr.target[OutputGroupInfo].gen_dir
    output_file = ctx.actions.declare_file("zstd")
    args = ctx.actions.args()
    zstd_dir = generated_dir.to_list()[0]
    args.add("%s/bin/zstd" % zstd_dir.path)
    args.add(output_file.path)
    ctx.actions.run(
        outputs = [output_file],
        inputs = [zstd_dir],
        arguments = [args],
        executable = "cp",
        mnemonic = "ZstdGetter",
    )
    return [DefaultInfo(
        executable = output_file,
        files = depset([output_file]),
    )]

zstd = rule(
    implementation = _zstd_impl,
    attrs = {
        "target": attr.label(
            allow_files = True,
        ),
    },
    executable = True,
)
