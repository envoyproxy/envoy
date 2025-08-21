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
#         "@com_github_ncopa_suexec//:su-exec": "utils/su-exec",
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

def _compilation_config_impl(settings, attr):
    _ignore = settings
    return {
        "//command_line_option:compilation_mode": attr.mode,
        "//command_line_option:stripopt": attr.stripopts,
    }

compilation_config = transition(
    implementation = _compilation_config_impl,
    inputs = [],
    outputs = [
        "//command_line_option:compilation_mode",
        "//command_line_option:stripopt",
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
