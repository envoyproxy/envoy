# `@envoy_repo` repository rule for managing the repo and querying its metadata.

CONTAINERS = """
PREFIX_MOBILE = "mobile"

REPO = "{repo}"
REPO_GCR = "{repo_gcr}"
SHA = "{sha}"
SHA_MOBILE = "{sha_mobile}"
TAG = "{tag}"

def image_mobile():
    return "%s:%s-%s@sha256:%s" % (
        REPO, PREFIX_MOBILE, TAG, SHA_MOBILE)

def image_worker():
    return "%s:%s@sha256:%s" % (
        REPO_GCR, TAG, SHA)

"""

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

    # parse container information for use in RBE
    json_result = repository_ctx.execute([
        repository_ctx.path(repository_ctx.attr.yq),
        repository_ctx.path(repository_ctx.attr.envoy_ci_config),
        "-ojson",
    ])
    if json_result.return_code != 0:
        fail("yq failed: {}".format(json_result.stderr))
    repository_ctx.file("ci-config.json", json_result.stdout)
    config_data = json.decode(repository_ctx.read("ci-config.json"))
    repository_ctx.file("containers.bzl", CONTAINERS.format(
        repo = config_data["build-image"]["repo"],
        repo_gcr = config_data["build-image"]["repo-gcr"],
        sha = config_data["build-image"]["sha"],
        sha_mobile = config_data["build-image"]["sha-mobile"],
        tag = config_data["build-image"]["tag"],
    ))
    repo_version_path = repository_ctx.path(repository_ctx.attr.envoy_version)
    api_version_path = repository_ctx.path(repository_ctx.attr.envoy_api_version)
    version = repository_ctx.read(repo_version_path).strip()
    api_version = repository_ctx.read(api_version_path).strip()
    repository_ctx.file("version.bzl", "VERSION = '%s'\nAPI_VERSION = '%s'" % (version, api_version))
    repository_ctx.file("path.bzl", "PATH = '%s'" % repo_version_path.dirname)
    repository_ctx.file("envoy_repo.py", "PATH = '%s'\nVERSION = '%s'\nAPI_VERSION = '%s'" % (repo_version_path.dirname, version, api_version))
    repository_ctx.file("WORKSPACE", "")
    repository_ctx.file("BUILD", '''
load("@rules_python//python:defs.bzl", "py_library")
load("@rules_python//python/entry_points:py_console_script_binary.bzl", "py_console_script_binary")
load("//:path.bzl", "PATH")

py_library(
    name = "envoy_repo",
    srcs = ["envoy_repo.py"],
    visibility = ["//visibility:public"],
)

py_console_script_binary(
    name = "get_project_json",
    pkg = "@base_pip3//envoy_base_utils",
    script = "envoy.project_data",
    data = [":envoy_repo.py"],
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

py_console_script_binary(
    name = "release",
    args = [
        "release",
        PATH,
        "--release-message-path=$(location @envoy//changelogs:summary)",
    ],
    data = [
        ":envoy_repo.py",
        "@envoy//changelogs:summary",
    ],
    pkg = "@base_pip3//envoy_base_utils",
    script = "envoy.project",
)

py_console_script_binary(
    name = "dev",
    args = [
        "dev",
        PATH,
    ],
    pkg = "@base_pip3//envoy_base_utils",
    script = "envoy.project",
    data = [":envoy_repo.py"],
)

py_console_script_binary(
    name = "sync",
    args = [
        "sync",
        PATH,
    ],
    pkg = "@base_pip3//envoy_base_utils",
    script = "envoy.project",
    data = [":envoy_repo.py"],
)

py_console_script_binary(
    name = "publish",
    args = [
        "publish",
        PATH,
    ],
    pkg = "@base_pip3//envoy_base_utils",
    script = "envoy.project",
    data = [":envoy_repo.py"],
)

py_console_script_binary(
    name = "trigger",
    args = [
        "trigger",
        PATH,
    ],
    pkg = "@base_pip3//envoy_base_utils",
    script = "envoy.project",
    data = [":envoy_repo.py"],
)

''')

_envoy_repo = repository_rule(
    implementation = _envoy_repo_impl,
    attrs = {
        "envoy_version": attr.label(default = "@envoy//:VERSION.txt"),
        "envoy_api_version": attr.label(default = "@envoy//:API_VERSION.txt"),
        "envoy_ci_config": attr.label(default = "@envoy//:.github/config.yml"),
        "yq": attr.label(default = "@yq"),
    },
)

def envoy_repo():
    if "envoy_repo" not in native.existing_rules().keys():
        _envoy_repo(name = "envoy_repo")
