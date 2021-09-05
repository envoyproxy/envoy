def _envoy_repo_impl(repository_ctx):
    """This provides information about the Envoy repository

    You can access the current version and path to the repository in .bzl/BUILD
    files as follows:

    ```starlark

    load("@envoy_repo//:version.bzl", "VERSION")
    load("@envoy_repo//:path.bzl", "PATH")

    ```
    `VERSION` can be used to derive version-specific rules and can be passed
    to the rules.

    As the `PATH` is local to the machine, it is generally only useful for
    jobs that will run locally.

    This can be useful for example, for tooling that needs to check the
    repository, or to run bazel queries that cannot be run within the
    constraints of a `genquery`.

    """
    repo_path = repository_ctx.path(repository_ctx.attr.envoy_root).dirname
    version = repository_ctx.read(repo_path.get_child("VERSION")).strip()
    repository_ctx.file("version.bzl", "VERSION = '%s'" % version)
    repository_ctx.file("path.bzl", "PATH = '%s'" % repo_path)
    repository_ctx.file("__init__.py", "PATH = '%s'\nVERSION = '%s'" % (repo_path, version))
    repository_ctx.file("WORKSPACE", "")
    repository_ctx.file("BUILD", """
load("@rules_python//python:defs.bzl", "py_library")

py_library(name = "envoy_repo", srcs = ["__init__.py"], visibility = ["//visibility:public"])

""")

envoy_repo = repository_rule(
    implementation = _envoy_repo_impl,
    attrs = {
        "envoy_root": attr.label(default = "@envoy//:BUILD"),
    },
)

def envoy_repo_binding():
    if "envoy_repo" not in native.existing_rules().keys():
        envoy_repo(name = "envoy_repo")
