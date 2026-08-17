"""Starlark rule for generating a router config fuzzer corpus directory."""

def _corpus_from_test_impl(ctx):
    # Declare a directory tree artifact for the corpus output.
    corpus_dir = ctx.actions.declare_directory(ctx.label.name)

    ctx.actions.run(
        executable = ctx.executable.script,
        # The test binary is deliberately passed as an argument (cfg = "target"),
        # not as a tool (cfg = "exec"), to preserve the host/target distinction
        # needed for oss-fuzz builds.
        arguments = [ctx.executable.test_binary.path],
        inputs = [ctx.executable.test_binary],
        outputs = [corpus_dir],
        env = {"GENRULE_OUTPUT_DIR": corpus_dir.path},
        execution_requirements = ctx.attr.exec_properties,
        mnemonic = "CorpusFromTest",
        progress_message = "Generating corpus from %s" % ctx.label,
    )

    return [DefaultInfo(files = depset([corpus_dir]))]

corpus_from_test = rule(
    implementation = _corpus_from_test_impl,
    attrs = {
        "script": attr.label(
            executable = True,
            cfg = "exec",
            allow_single_file = True,
        ),
        "test_binary": attr.label(
            executable = True,
            # cfg = "target" (not "exec") to preserve host/target distinction
            # needed for oss-fuzz builds.
            cfg = "target",
            allow_single_file = True,
        ),
        "exec_properties": attr.string_dict(),
    },
    doc = "Runs a test binary to generate a corpus directory tree artifact.",
)
