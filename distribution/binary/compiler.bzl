# Compile targets using a bazel-defined compilation mode.
#
# This is useful for building packages comprised of binaries built in
# multiple modes - eg a release containing both `opt` and `dbg` builds.
#
# In this case the cli-supplied `-c ...` flag is ignored.
#
# NB: Avoid use of this as it creates a large sub-graph independent
#  of the normal bazel build graph.
#
#  See https://bazel.build/extending/config#memory-performance-considerations
#
# The `bundled` rule allows you to define a set of compilation targets that you wish to
# be compiled with compilation settings defined in bazel, that would normally be
# set on the command line.
#
# The `mode` sets the `compilation_mode` - one of `dbg`, `opt`, or `fastbuild`.
#
# You can define the `targets` to be compiled, along with the destination `path`
# of the compiled binary.
#
# You can also define lists of `stripopts`.
#
# For example, you can bundle some stripped binaries together in `opt` mode:
#
# ```starlark
#
# bundled(
#     name = "envoy",
#     mode = "opt",
#     stripopts = ["--strip-all"],
#     targets = {
#         "//distribution:envoy-binary": "envoy",
#         "//distribution:envoy-contrib-binary": "envoy-contrib",
#         "@su-exec": "utils/su-exec",
#     },
# )
#
#
# The bundled output can then be packaged together with other, differently compiled, output:
#
# ```starlark
#
# bundled(
#     name = "envoy-debug",
#     mode = "dbg",
#     targets = {
#         "//distribution:envoy-binary": "envoy",
#         "//distribution:envoy-dwarf": "envoy.dwp",
#         "//distribution:envoy-contrib-binary": "envoy-contrib",
#         "//distribution:envoy-contrib-dwarf": "envoy-contrib.dwp",
#     },
# )
#
# ```
#

# With `//distribution/binary:arm64-exec-platform` set (to an execution platform with arm64 workers,
# eg `//bazel/platforms/rbe:linux_arm64`), the targets of an arm64 build prefer it, so that they are
# built on arm64 workers, natively, whatever the host is, rather than cross-compiled: this branch's
# `rules_foreign_cc` dependencies (eg ICU, LuaJIT) can not be. It needs a toolchain for that platform
# (`--extra_toolchains=@llvm_toolchain_arm64//:all`).
#
# Only the bundled targets (the binaries) are built there: what packages them is not, and runs as it
# does for any other build (locally, on the host).

_ARM64_EXEC_PLATFORM = "//distribution/binary:arm64-exec-platform"
_ARM64_WORKERS = "//bazel:arm64-workers"
_EXEC_PLATFORMS = "//command_line_option:extra_execution_platforms"
_PLATFORMS = "//command_line_option:platforms"

def _compilation_config_impl(settings, attr):
    exec_platforms = settings[_EXEC_PLATFORMS]
    arm64_exec_platform = settings[_ARM64_EXEC_PLATFORM]
    arm64_build = any([str(platform).endswith("_arm64") for platform in settings[_PLATFORMS]])
    arm64_workers = bool(arm64_exec_platform and arm64_build)
    if arm64_workers:
        exec_platforms = [arm64_exec_platform] + [
            platform
            for platform in exec_platforms
            if str(platform) != arm64_exec_platform
        ]
    return {
        "//command_line_option:compilation_mode": attr.mode,
        "//command_line_option:stripopt": attr.stripopts,
        # For what the build runs from the LLVM distribution (`//bazel:arm64_workers`).
        _ARM64_WORKERS: arm64_workers,
        _EXEC_PLATFORMS: exec_platforms,
    }

compilation_config = transition(
    implementation = _compilation_config_impl,
    inputs = [
        _ARM64_EXEC_PLATFORM,
        _EXEC_PLATFORMS,
        _PLATFORMS,
    ],
    outputs = [
        "//command_line_option:compilation_mode",
        "//command_line_option:stripopt",
        _ARM64_WORKERS,
        _EXEC_PLATFORMS,
    ],
)

def _bundled_impl(ctx, **kwargs):
    output_files = []
    for i, target in enumerate(ctx.files.targets):
        path = ctx.attr.targets.values()[i]
        output_file = ctx.actions.declare_file(
            "%s/%s/%s" %
            (
                ctx.attr.name,
                ctx.var["COMPILATION_MODE"],
                path,
            ),
        )
        ctx.actions.symlink(
            output = output_file,
            target_file = target,
            progress_message = "Bundling %s -> %s" % (target.path, output_file.path),
        )
        output_files.append(output_file)
    return [DefaultInfo(files = depset(output_files))]

bundled = rule(
    implementation = _bundled_impl,
    attrs = {
        "mode": attr.string(),
        "stripopts": attr.string_list(),
        "targets": attr.label_keyed_string_dict(
            allow_files = True,
            allow_empty = False,
            mandatory = True,
        ),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
    cfg = compilation_config,
)
