#
# This rule generates and runs a shell script to create symbolic links for the specified binaries.
#
# This is useful in a vscode devcontainer, where tools must be directly executable.
#

def _create_tool_links_impl(ctx):
    output_script = ctx.actions.declare_file(ctx.label.name + ".sh")

    lines = ["#!/bin/bash", "set -e"]

    for tool in ctx.files.tools:
        lines.append("ln -sf $(realpath {src}) /usr/local/bin/{base}".format(
            src = tool.path,
            base = tool.basename,
        ))

    ctx.actions.write(
        output = output_script,
        content = "\n".join(lines),
        is_executable = True,
    )

    return DefaultInfo(
        executable = output_script,
        runfiles = ctx.runfiles(files = ctx.files.tools),
    )

create_tool_links = rule(
    implementation = _create_tool_links_impl,
    attrs = {
        "tools": attr.label_list(allow_files = True),
    },
    executable = True,
)
