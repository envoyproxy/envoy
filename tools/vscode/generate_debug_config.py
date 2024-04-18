#!/usr/bin/env python3

import argparse
import json
import os
import pathlib
import shlex
import shutil
import subprocess

BAZEL_OPTIONS = shlex.split(os.environ.get("BAZEL_BUILD_OPTION_LIST", ""))
BAZEL_STARTUP_OPTIONS = shlex.split(os.environ.get("BAZEL_STARTUP_OPTION_LIST", ""))


def bazel_info(name, bazel_extra_options=[]):
    return subprocess.check_output(["bazel", *BAZEL_STARTUP_OPTIONS, "info", name] + BAZEL_OPTIONS
                                   + bazel_extra_options).decode().strip()


def get_workspace():
    return bazel_info("workspace")


def get_execution_root(workspace):
    # If compilation database exists, use its execution root, this allows setting
    # breakpoints with clangd navigation easier.
    try:
        compdb = pathlib.Path(workspace, "compile_commands.json").read_text()
        return json.loads(compdb)[0]['directory']
    except:
        return bazel_info("execution_root")


def binary_path(bazel_bin, target):
    return pathlib.Path(
        bazel_bin,
        *[s for s in target.replace('@', 'external/').replace(':', '/').split('/') if s != ''])


def build_binary_with_debug_info(target):
    subprocess.check_call(["bazel", *BAZEL_STARTUP_OPTIONS, "build", "-c", "dbg", target]
                          + BAZEL_OPTIONS)

    bazel_bin = bazel_info("bazel-bin", ["-c", "dbg"])
    return binary_path(bazel_bin, target)


def get_launch_json(workspace):
    try:
        return json.loads(pathlib.Path(workspace, ".vscode", "launch.json").read_text())
    except:
        return {"version": "0.2.0"}


def write_launch_json(workspace, launch):
    launch_json = pathlib.Path(workspace, ".vscode", "launch.json")
    backup_launch_json = pathlib.Path(workspace, ".vscode", "launch.json.bak")
    if launch_json.exists():
        shutil.move(str(launch_json), str(backup_launch_json))

    launch_json.write_text(json.dumps(launch, indent=4))


def gdb_config(target, binary, workspace, execroot, arguments):
    return {
        "name": "gdb " + target,
        "request": "launch",
        "arguments": arguments,
        "type": "gdb",
        "target": str(binary),
        "debugger_args": ["--directory=" + execroot],
        "cwd": "${workspaceFolder}",
        "valuesFormatting": "disabled"
    }


def lldb_config(target, binary, workspace, execroot, arguments):
    return {
        "name": "lldb " + target,
        "program": str(binary),
        "sourceMap": {
            "/proc/self/cwd": workspace,
            "/proc/self/cwd/external": execroot + "/external",
            "/proc/self/cwd/bazel-out": execroot + "/bazel-out"
        },
        "cwd": "${workspaceFolder}",
        "args": shlex.split(arguments),
        "type": "lldb",
        "request": "launch"
    }


def add_to_launch_json(target, binary, workspace, execroot, arguments, debugger_type, overwrite):
    launch = get_launch_json(workspace)
    new_config = {}
    always_overwritten_fields = []
    if debugger_type == "lldb":
        always_overwritten_fields = ["program", "sourceMap", "cwd", "type", "request"]
        new_config = lldb_config(target, binary, workspace, execroot, arguments)
    else:
        always_overwritten_fields = [
            "request", "type", "target", "debugger_args", "cwd", "valuesFormatting"
        ]
        new_config = gdb_config(target, binary, workspace, execroot, arguments)

    configurations = launch.get("configurations", [])
    for config in configurations:
        if config.get("name", None) == new_config["name"]:
            if overwrite:
                config.clear()
                config.update(new_config)
            else:
                for k in always_overwritten_fields:
                    config[k] = new_config[k]
                print(
                    f"old config exists, only {always_overwritten_fields} will be updated, use --overwrite to recreate config"
                )
            break
    else:
        configurations.append(new_config)

    launch["configurations"] = configurations
    write_launch_json(workspace, launch)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Build and generate launch config for VSCode')
    parser.add_argument('--debugger', default="gdb", help="debugger type, one of [gdb, lldb]")
    parser.add_argument('--args', default='', help="command line arguments if target binary")
    parser.add_argument(
        '--overwrite',
        action="store_true",
        help="recreate config without preserving any existing config")
    parser.add_argument('target', help="target binary which you want to build")
    args = parser.parse_args()

    workspace = get_workspace()
    execution_root = get_execution_root(workspace)
    debug_binary = build_binary_with_debug_info(args.target)
    add_to_launch_json(
        args.target, debug_binary, workspace, execution_root, args.args, args.debugger,
        args.overwrite)
