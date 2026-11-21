"""Compress the debug sections of a binary or `.dwp`, with the C++ toolchain's objcopy."""

load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain", "use_cpp_toolchain")

def _compressed_debug_parts_impl(ctx):
    cc_toolchain = find_cpp_toolchain(ctx)
    src = ctx.file.src
    parts = [
        ctx.actions.declare_file("%s/part%d" % (ctx.label.name, i))
        for i in range(ctx.attr.parts)
    ]
    prefix = parts[0].path[:-1]

    # Compressed and split in one action, so that the compressed file is never a blob of its own:
    # only the parts are uploaded, and nothing has to fetch the whole file to split it.
    ctx.actions.run_shell(
        command = """
set -e
"$1" --compress-debug-sections="$2" "$3" "$4.tmp"
split -n "$5" -d -a 1 "$4.tmp" "$4"
rm -f "$4.tmp"
""",
        arguments = [
            cc_toolchain.objcopy_executable,
            ctx.attr.compression,
            src.path,
            prefix,
            str(ctx.attr.parts),
        ],
        inputs = depset([src], transitive = [cc_toolchain.all_files]),
        outputs = parts,
        mnemonic = "CompressDebugSections",
        progress_message = "Compressing the debug sections of %s, in %d parts" % (src.short_path, ctx.attr.parts),
    )
    return [DefaultInfo(files = depset(parts))]

compressed_debug_parts = rule(
    implementation = _compressed_debug_parts_impl,
    doc = """`src` (eg a `.dwp`, which the link's `--compress-debug-sections` does not reach) with its
debug sections compressed, as debuggers read them, split into `parts` (`part0`...) that concatenate,
in order, to the compressed file.""",
    attrs = {
        "compression": attr.string(
            default = "zstd",
            values = [
                "zlib",
                "zstd",
            ],
        ),
        "parts": attr.int(
            default = 4,
            values = list(range(1, 11)),
        ),
        "src": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
    },
    fragments = ["cpp"],
    toolchains = use_cpp_toolchain(),
)
