load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")
load("@dynamic_modules_rust_sdk_crate_index//:defs.bzl", "crate_repositories")
load("@platforms//host:extension.bzl", "host_platform_repo")

# Dependencies that rely on a first stage of envoy_dependency_imports() in dependency_imports.bzl.
def envoy_dependency_imports_extra():
    crate_repositories()
    maybe(
        host_platform_repo,
        name = "host_platform",
    )
