"""Strip a binary on the host, with the host's C++ toolchain."""

load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain", "use_cpp_toolchain")

def _host_stripped_impl(ctx):
    cc_toolchain = find_cpp_toolchain(ctx)
    src = ctx.file.src
    out = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.run(
        executable = cc_toolchain.strip_executable,
        arguments = ctx.attr.stripopts + ["-o", out.path, src.path],
        inputs = depset([src], transitive = [cc_toolchain.all_files]),
        outputs = [out],
        mnemonic = "HostStrip",
        progress_message = "Stripping %{input}",
    )
    return [DefaultInfo(files = depset([out]))]

host_stripped = rule(
    implementation = _host_stripped_impl,
    doc = """`src` stripped, as its `.stripped` output is, but by this target's C++ toolchain.

A binary's `.stripped` is stripped by the toolchain of the platform that the binary is built on.
For a binary built on other workers than the host's (eg arm64 from an x64 host, see `compiler.bzl`)
that strip does not run on the host, so stripping it has to run remotely too. This target is
resolved for the host (`exec_compatible_with`), with its strip, which strips a binary of any arch,
so it can run locally: the binary is downloaded for packaging anyway.""",
    attrs = {
        "src": attr.label(
            allow_single_file = True,
            doc = "The binary.",
            mandatory = True,
        ),
        "stripopts": attr.string_list(
            default = ["--strip-all"],
            doc = "Options for the strip.",
        ),
    },
    toolchains = use_cpp_toolchain(),
)
