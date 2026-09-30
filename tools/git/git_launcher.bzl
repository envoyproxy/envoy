"""Executable wrapper around the hermetic git toolchain.

This allows shell callers to invoke the toolchain-provided `git` with
`bazel run //tools/git:git -- <args>`, without requiring `git` on the host.
"""

load("@envoy_toolshed//git:defs.bzl", "GIT_TOOLCHAIN_TYPE")

# Substituted with `.replace()` rather than `.format()`: the script's own
# `${VAR:-}` expansions would otherwise be parsed as format placeholders.
_LAUNCHER_TEMPLATE = """#!/bin/bash
set -euo pipefail
self="$0"
case "$self" in
    /*) ;;
    *) self="$(pwd)/$self" ;;
esac
if [[ -n "${TEST_SRCDIR:-}" ]]; then
    runfiles="$TEST_SRCDIR"
elif [[ -n "${RUNFILES_DIR:-}" ]]; then
    runfiles="$RUNFILES_DIR"
elif [[ -d "$self.runfiles" ]]; then
    runfiles="$self.runfiles"
else
    launcher="/@@LAUNCHER@@"
    case "$self" in
        *"$launcher") runfiles="${self%"$launcher"}" ;;
        *) runfiles="$(CDPATH= cd "$(dirname "$self")" && pwd)" ;;
    esac
fi
case "$runfiles" in
    /*) ;;
    *) runfiles="$(pwd)/$runfiles" ;;
esac
git="$runfiles/@@GIT@@"
if [[ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]]; then
    cd "$BUILD_WORKSPACE_DIRECTORY"
fi
exec "$git" "$@"
"""

def _runfile_path(ctx, file_):
    if file_.short_path.startswith("../"):
        return file_.short_path[3:]
    return "%s/%s" % (ctx.workspace_name, file_.short_path)

def _git_launcher_impl(ctx):
    git_info = ctx.toolchains[GIT_TOOLCHAIN_TYPE].git
    launcher = ctx.actions.declare_file(ctx.label.name + ".sh")
    ctx.actions.write(
        launcher,
        _LAUNCHER_TEMPLATE
            .replace("@@GIT@@", _runfile_path(ctx, git_info.git))
            .replace("@@LAUNCHER@@", _runfile_path(ctx, launcher)),
        is_executable = True,
    )
    return [
        DefaultInfo(
            executable = launcher,
            files = depset([launcher]),
            runfiles = git_info.runfiles.merge(ctx.runfiles(files = [git_info.git])),
        ),
    ]

git_launcher = rule(
    implementation = _git_launcher_impl,
    doc = "Emits an executable launcher for the resolved git toolchain.",
    executable = True,
    toolchains = [GIT_TOOLCHAIN_TYPE],
)

def _git_runtime_impl(ctx):
    git_info = ctx.toolchains[GIT_TOOLCHAIN_TYPE].git
    files = git_info.runfiles.merge(ctx.runfiles(files = [git_info.git])).files
    return [DefaultInfo(files = files)]

git_runtime = rule(
    implementation = _git_runtime_impl,
    doc = "Exposes the resolved git toolchain runtime files, for `$(GIT)` consumers.",
    toolchains = [GIT_TOOLCHAIN_TYPE],
)
