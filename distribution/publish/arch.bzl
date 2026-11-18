"""Everything published for an arch, from any host (`ci/do_ci.sh release.multiarch`)."""

def _arch_transition_impl(_settings, attr):
    return {"//command_line_option:platforms": [attr.platform]}

_arch_transition = transition(
    implementation = _arch_transition_impl,
    inputs = [],
    outputs = ["//command_line_option:platforms"],
)

def _arch_files_impl(ctx):
    outputs = {}
    for file in ctx.files.srcs:
        if file.basename in outputs:
            fail("publish: %s is published twice for %s" % (file.basename, ctx.attr.platform))
        output = ctx.actions.declare_file("%s/%s" % (ctx.label.name, file.basename))
        ctx.actions.symlink(output = output, target_file = file)
        outputs[file.basename] = output
    return [DefaultInfo(files = depset(outputs.values()))]

arch_files = rule(
    implementation = _arch_files_impl,
    doc = """`srcs` built for `platform`, at `<name>/<basename>`.

Only the target platform changes: everything runs on the build's execution platforms, so the
binaries of another arch are cross-compiled (eg arm64 from an x64 host).""",
    attrs = {
        "platform": attr.string(
            doc = "The target platform.",
            mandatory = True,
        ),
        "srcs": attr.label_list(
            allow_files = True,
            cfg = _arch_transition,
            mandatory = True,
        ),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)

def _arch_file_impl(ctx):
    files = [file for file in ctx.files.src if file.basename == ctx.attr.basename]
    if len(files) != 1:
        fail("publish: expected one %s in %s, found %d" % (ctx.attr.basename, ctx.attr.src.label, len(files)))
    return [DefaultInfo(files = depset(files))]

arch_file = rule(
    implementation = _arch_file_impl,
    doc = """The file named `basename` of `src` (an `arch_files`), eg a signing input
(`//distribution:<arch>-release`, `//distribution:<arch>-packages`).""",
    attrs = {
        "basename": attr.string(mandatory = True),
        "src": attr.label(mandatory = True),
    },
)
