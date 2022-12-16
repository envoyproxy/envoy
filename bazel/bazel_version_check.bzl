load("@bazel_skylib//lib:versions.bzl", "versions")

def _impl(repository_ctx):
    expected_version = repository_ctx.read(repository_ctx.attr._version_file).strip()
    current_version = native.bazel_version

    if current_version.split(".") < expected_version.split("."):
        fail("Current bazel version '{}' is too old, expected at least '{}'. Install and use bazelisk for easier bazel version management: https://github.com/bazelbuild/bazelisk".format(current_version, expected_version))

    # Create a function for calling in the WORKSPACE to force evaluation
    repository_ctx.file("BUILD.bazel", "")
    repository_ctx.file("version.bzl", """
def check_bazel_version():
    pass
""")

setup_bazel_version_check = repository_rule(
    implementation = _impl,
    attrs = {
        "_version_file": attr.label(default = Label("@//:.bazelversion")),
    },
)
