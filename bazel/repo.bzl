# `@envoy_repo` repository rule for managing the repo and querying its metadata.

def _envoy_repo_impl(repository_ctx):
    """This provides information about the Envoy repository

    You can access the current project and api versions and the path to the repository in
    .bzl/BUILD files as follows:

    ```starlark
    load("@envoy_repo//:version.bzl", "VERSION", "API_VERSION")
    ```

    `*VERSION` can be used to derive version-specific rules and can be passed
    to the rules.

    The `VERSION`s and also the local `PATH` to the repo can be accessed in
    python libraries/binaries. By adding `@envoy_repo` to `deps` they become
    importable through the `envoy_repo` namespace.

    As the `PATH` is local to the machine, it is generally only useful for
    jobs that will run locally.

    This can be useful, for example, for bazel run jobs to run bazel queries that cannot be run
    within the constraints of a `genquery`, or that otherwise need access to the repository
    files.

    Project and repo data can be accessed in JSON format using `@envoy_repo//:project`, eg:

    ```starlark
    load("@aspect_bazel_lib//lib:jq.bzl", "jq")

    jq(
        name = "project_version",
        srcs = ["@envoy_repo//:data"],
        out = "version.txt",
        args = ["-r"],
        filter = ".version",
    )

    ```

    """
    repo_version_path = repository_ctx.path(repository_ctx.attr.envoy_version)
    api_version_path = repository_ctx.path(repository_ctx.attr.envoy_api_version)
    version = repository_ctx.read(repo_version_path).strip()
    api_version = repository_ctx.read(api_version_path).strip()
    repository_ctx.file("version.bzl", "VERSION = '%s'\nAPI_VERSION = '%s'" % (version, api_version))
    repository_ctx.file("path.bzl", "PATH = '%s'" % repo_version_path.dirname)
    repository_ctx.file("__init__.py", "PATH = '%s'\nVERSION = '%s'\nAPI_VERSION = '%s'" % (repo_version_path.dirname, version, api_version))
    repository_ctx.file("WORKSPACE", "")
    repository_ctx.file("BUILD", '''
load("@rules_python//python:defs.bzl", "py_library")
load("@envoy//tools/base:envoy_python.bzl", "envoy_entry_point")
load("//:path.bzl", "PATH")

py_library(
    name = "envoy_repo",
    srcs = ["__init__.py"],
    visibility = ["//visibility:public"],
)

envoy_entry_point(
    name = "get_project_json",
    pkg = "envoy.base.utils",
    script = "envoy.project_data",
    init_data = [":__init__.py"],
)

genrule(
    name = "generate_release_hash_bin",
    outs = ["generate_release_hash.sh"],
    cmd = """
    echo "
#!/usr/bin/env bash

set -e -o pipefail

git ls-remote --tags https://github.com/envoyproxy/envoy \\\\
    | grep -E 'refs/tags/v[0-9]+\\\\.[0-9]+\\\\.[0-9]+$$' \\\\
    | sort -u \\\\
    | sha256sum \\\\
    | cut -d ' ' -f 1" > $@
    chmod +x $@
    """
)

sh_binary(
    name = "generate_release_hash",
    srcs = [":generate_release_hash_bin"],
    visibility = ["//visibility:public"],
)

# This sets a default hash based on currently visible tagged versions.
# Its very questionably hermetic, making assumptions about git, the repo remotes and so on.
# The general idea here is to make this cache blow any time there are release changes.
# You can use the above sh_binary to generate a custom/correct hash to override below.
genrule(
    name = "default_release_hash",
    outs = ["default_release_hash.txt"],
    cmd = """
    $(location :generate_release_hash) > $@
    """,
    stamp = True,
    tags = ["no-remote-exec"],
    tools = [":generate_release_hash"],
)

label_flag(
    name = "release-hash",
    build_setting_default = ":default_release_hash",
    visibility = ["//visibility:public"],
)

genrule(
    name = "project",
    outs = ["project.json"],
    cmd = """
    $(location :get_project_json) $$(dirname $(location @envoy//:VERSION.txt)) > $@
    """,
    tools = [
        ":get_project_json",
        ":release-hash",
        "@envoy//:VERSION.txt",
        "@envoy//changelogs",
    ],
    visibility = ["//visibility:public"],
)

envoy_entry_point(
    name = "release",
    args = [
        "release",
        PATH,
        "--release-message-path=$(location @envoy//changelogs:summary)",
    ],
    data = ["@envoy//changelogs:summary"],
    pkg = "envoy.base.utils",
    script = "envoy.project",
    init_data = [":__init__.py"],
)

envoy_entry_point(
    name = "dev",
    args = [
        "dev",
        PATH,
    ],
    pkg = "envoy.base.utils",
    script = "envoy.project",
    init_data = [":__init__.py"],
)

envoy_entry_point(
    name = "sync",
    args = [
        "sync",
        PATH,
    ],
    pkg = "envoy.base.utils",
    script = "envoy.project",
    init_data = [":__init__.py"],
)

envoy_entry_point(
    name = "publish",
    args = [
        "publish",
        PATH,
    ],
    pkg = "envoy.base.utils",
    script = "envoy.project",
    init_data = [":__init__.py"],
)

envoy_entry_point(
    name = "trigger",
    args = [
        "trigger",
        PATH,
    ],
    pkg = "envoy.base.utils",
    script = "envoy.project",
    init_data = [":__init__.py"],
)

''')

_envoy_repo = repository_rule(
    implementation = _envoy_repo_impl,
    attrs = {
        "envoy_version": attr.label(default = "@envoy//:VERSION.txt"),
        "envoy_api_version": attr.label(default = "@envoy//:API_VERSION.txt"),
    },
)

def envoy_repo():
    if "envoy_repo" not in native.existing_rules().keys():
        _envoy_repo(name = "envoy_repo")
