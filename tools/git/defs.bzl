"""Targets exposing the hermetic git toolchain, so nothing depends on a host `git`."""

load("@envoy_toolshed//git:defs.bzl", "GIT_TOOLCHAIN_TYPE")

def _git_runtime_impl(ctx):
    git_info = ctx.toolchains[GIT_TOOLCHAIN_TYPE].git
    files = git_info.runfiles.merge(ctx.runfiles(files = [git_info.git])).files
    return [DefaultInfo(files = files)]

git_runtime = rule(
    implementation = _git_runtime_impl,
    doc = "Exposes the resolved git toolchain runtime files, for `$(GIT)` consumers.",
    toolchains = [GIT_TOOLCHAIN_TYPE],
)

def _git_bin_impl(ctx):
    return [DefaultInfo(files = depset([ctx.toolchains[GIT_TOOLCHAIN_TYPE].git.git]))]

git_bin = rule(
    implementation = _git_bin_impl,
    doc = "Exposes just the resolved git toolchain executable, eg to resolve `GIT_BIN` in CI.",
    toolchains = [GIT_TOOLCHAIN_TYPE],
)
