"""
Rule to create objdump debug info from a native dynamic library built for
Android.

This is a workaround for generally not being able to produce dwp files for
Android https://github.com/bazelbuild/bazel/pull/14765

But even if we could create those we'd need to get them out of the build
somehow, this rule provides a separate --output_group for this
"""

def _impl(ctx):
    library_outputs = []
    objdump_outputs = []
    for platform, dep in ctx.split_attr.dep.items():
        # When --fat_apk_cpu isn't set, the platform is None
        if len(dep.files.to_list()) != 1:
            fail("Expected exactly one file in the library")

        cc_toolchain = ctx.split_attr._cc_toolchain[platform][cc_common.CcToolchainInfo]
        lib = dep.files.to_list()[0]
        platform_name = platform or ctx.fragments.android.android_cpu
        objdump_output = ctx.actions.declare_file(platform_name + "/" + platform_name + ".objdump.gz")

        ctx.actions.run_shell(
            inputs = [lib],
            outputs = [objdump_output],
            command = cc_toolchain.objdump_executable + " --syms " + lib.path + "| gzip -c >" + objdump_output.path,
            tools = [cc_toolchain.all_files],
            progress_message = "Generating symbol map " + platform_name,
        )

        strip_output = ctx.actions.declare_file(platform_name + "/" + lib.basename)
        ctx.actions.run_shell(
            inputs = [lib],
            outputs = [strip_output],
            command = cc_toolchain.strip_executable + " --strip-all " + lib.path + " -o " + strip_output.path,
            tools = [cc_toolchain.all_files],
            progress_message = "Stripping library " + lib.path,
        )

        library_outputs.append(strip_output)
        objdump_outputs.append(objdump_output)

    return [
        DefaultInfo(files = depset(library_outputs)),
        OutputGroupInfo(objdump = objdump_outputs),
    ]

android_debug_info = rule(
    implementation = _impl,
    attrs = dict(
        dep = attr.label(
            providers = [CcInfo],
            cfg = android_common.multi_cpu_configuration,
        ),
        _cc_toolchain = attr.label(
            default = Label("@bazel_tools//tools/cpp:current_cc_toolchain"),
            cfg = android_common.multi_cpu_configuration,
        ),
    ),
    fragments = ["cpp", "android"],
)
