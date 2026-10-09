"""Starlark rule for generating a router config fuzzer corpus directory."""

def _corpus_from_test_binary_impl(ctx):
    # Declare a directory tree artifact for the corpus output.
    corpus_dir = ctx.actions.declare_directory(ctx.label.name)

    # The test binary is passed as an argument rather than as a tool, so its
    # own files and runfiles must be staged as action inputs explicitly.
    test_binary_info = ctx.attr.test_binary[DefaultInfo]
    inputs = depset(
        [ctx.executable.test_binary],
        transitive = [
            test_binary_info.files,
            test_binary_info.default_runfiles.files,
        ],
    )

    ctx.actions.run(
        executable = ctx.executable.script,
        # The test binary is deliberately passed as an argument (cfg = "target"),
        # not as a tool (cfg = "exec"), to preserve the host/target distinction
        # needed for oss-fuzz builds.
        arguments = [ctx.executable.test_binary.path],
        inputs = inputs,
        # Passing the script via `tools` ensures its runfiles are staged.
        tools = [ctx.attr.script[DefaultInfo].files_to_run],
        outputs = [corpus_dir],
        env = {"GENRULE_OUTPUT_DIR": corpus_dir.path},
        use_default_shell_env = True,
        mnemonic = "CorpusFromTest",
        progress_message = "Generating corpus from %s" % ctx.label,
    )

    return [DefaultInfo(files = depset([corpus_dir]))]

# Note: the rule name must not end in `_test` - that suffix is reserved by
# Bazel for test rule classes.
#
# Note: `exec_properties` is not declared here - it is a built-in attribute
# provided by Bazel on all rules, and is propagated to the rule's actions
# automatically. Declaring it explicitly is an error.
corpus_from_test_binary = rule(
    implementation = _corpus_from_test_binary_impl,
    attrs = {
        # No `allow_single_file` on these - executable targets produce an
        # executable *and* a runfiles tree, not a single file.
        "script": attr.label(
            executable = True,
            cfg = "exec",
            mandatory = True,
        ),
        "test_binary": attr.label(
            executable = True,
            # cfg = "target" (not "exec") to preserve host/target distinction
            # needed for oss-fuzz builds.
            cfg = "target",
            mandatory = True,
        ),
    },
    doc = "Runs a test binary to generate a corpus directory tree artifact.",
)
