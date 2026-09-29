"""Build Sphinx HTML archives."""

# TODO(phlax): Move to toolshed

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
    build_sha = ctx.attr._build_sha[BuildSettingInfo].value
    docs_tag = ctx.attr._docs_tag[BuildSettingInfo].value

    ctx.actions.run_shell(
        inputs = inputs,
        tools = [ctx.attr.sphinx_runner[DefaultInfo].files_to_run],
        outputs = [ctx.outputs.out],
        arguments = [
            "1" if ctx.attr.stamp else "0",
            volatile_env.path if volatile_env else "",
            build_sha,
            docs_tag,
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
            fi
            if [[ -n "$3" ]]; then
                build_sha="$3"
            elif [[ "$1" == "1" ]]; then
                build_sha="${BUILD_DOCS_SHA:-${ENVOY_BUILD_SCM_REVISION:-${BUILD_SCM_REVISION}}}"
            else
                build_sha="${BUILD_DOCS_SHA:-}"
            fi
            build_sha_arg=()
            if [[ -n "$build_sha" ]]; then
                build_sha_arg=(--build_sha="$build_sha")
            fi
            docs_tag="$4"
            if [[ -z "$docs_tag" && "$1" == "1" ]]; then
                docs_tag="${BUILD_DOCS_TAG:-}"
            fi
            docs_tag_arg=()
            if [[ -n "$docs_tag" ]]; then
                docs_tag_arg=(--docs_tag="$docs_tag")
            fi
            runner="$5"
            version_file="$6"
            descriptor_path="$7"
            rst="$8"
            out="$9"
            shift 9
            "$runner" "$@" "${build_sha_arg[@]}" "${docs_tag_arg[@]}" \\
                --version_file="$version_file" --descriptor_path="$descriptor_path" "$rst" "$out"
        """,
        mnemonic = "SphinxHtml",
        use_default_shell_env = True,
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
        "_build_sha": attr.label(default = "//:build_sha"),
        "_docs_tag": attr.label(default = "//:docs_tag"),
        "_sphinx_args": attr.label(default = "//:sphinx_args"),
    },
)
