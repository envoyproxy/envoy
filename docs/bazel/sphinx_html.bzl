"""Build Sphinx HTML archives."""

load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")

def _sphinx_html_impl(ctx):
    rst = ctx.file.rst
    runner = ctx.executable.sphinx_runner
    version_file = ctx.file.version_file
    descriptor_path = ctx.file.descriptor_path
    inputs = [rst, version_file, descriptor_path]
    volatile_env = ctx.file.volatile_env if ctx.attr.stamp else None
    if volatile_env:
        inputs.extend([volatile_env, ctx.version_file])

    sphinx_args = ctx.attr._sphinx_args[BuildSettingInfo].value
    sphinx_args = sphinx_args.replace("\t", " ").replace("\n", " ").replace("\r", " ")

    ctx.actions.run_shell(
        inputs = inputs,
        tools = [ctx.attr.sphinx_runner[DefaultInfo].files_to_run],
        outputs = [ctx.outputs.out],
        arguments = [
            "1" if ctx.attr.stamp else "0",
            volatile_env.path if volatile_env else "",
            runner.path,
            version_file.path,
            descriptor_path.path,
            rst.path,
            ctx.outputs.out.path,
        ] + [arg for arg in sphinx_args.split(" ") if arg],
        command = ctx.attr.link_suffix_env + """
            set -e
            if [[ "$1" == "1" ]]; then
                . "$2"
                build_sha="${BUILD_DOCS_SHA:-${ENVOY_BUILD_SCM_REVISION:-${BUILD_SCM_REVISION}}}"
                docs_tag=(--docs_tag="${BUILD_DOCS_TAG:-}")
            else
                build_sha="${BUILD_DOCS_SHA:-}"
                docs_tag=()
            fi
            runner="$3"
            version_file="$4"
            descriptor_path="$5"
            rst="$6"
            out="$7"
            shift 7
            "$runner" "$@" --build_sha="$build_sha" "${docs_tag[@]}" \\
                --version_file="$version_file" --descriptor_path="$descriptor_path" "$rst" "$out"
        """,
        mnemonic = "SphinxHtml",
    )

sphinx_html = rule(
    implementation = _sphinx_html_impl,
    attrs = {
        "out": attr.output(mandatory = True),
        "rst": attr.label(mandatory = True, allow_single_file = True),
        "sphinx_runner": attr.label(mandatory = True, executable = True, cfg = "exec"),
        "version_file": attr.label(mandatory = True, allow_single_file = True),
        "descriptor_path": attr.label(mandatory = True, allow_single_file = True),
        "link_suffix_env": attr.string(),
        "stamp": attr.int(default = 0, values = [0, 1]),
        "volatile_env": attr.label(allow_single_file = True),
        "_sphinx_args": attr.label(default = "//:sphinx_args"),
    },
)
