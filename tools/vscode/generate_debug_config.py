#!/usr/bin/env python3

import argparse
import json
import os
import pathlib
import platform
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


def build_binary_with_debug_info(target, config=None):
    build_args = ["bazel", *BAZEL_STARTUP_OPTIONS, "build"]
    info_args = []

    if config:
        build_args.extend([f"--config={config}"])
        info_args.extend([f"--config={config}"])

    build_args.extend(["-c", "dbg", target])
    build_args.extend(BAZEL_OPTIONS)

    subprocess.check_call(build_args)

    bazel_bin = bazel_info("bazel-bin", info_args + ["-c", "dbg"])
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
    cfg = {
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

    # https://github.com/vadimcn/codelldb/discussions/517
    if platform.system() == "Darwin" and platform.machine() == "arm64":
        cfg["sourceMap"] = {
            ".": "${workspaceFolder}",
        }
    return cfg


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


def auto_detect_config():
    """Auto-detect the best compiler configuration based on platform and availability."""
    try:
        # Check if we're on ARM64 architecture
        if platform.machine() in ['aarch64', 'arm64']:
            # On ARM64, prefer clang for better compatibility
            return "clang"
        # On other architectures, try to detect available compilers
        # This is a simple heuristic - could be made more sophisticated
        return None  # Let Bazel use its default
    except:
        return None


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Build and generate launch config for VSCode')
    parser.add_argument('--debugger', default="gdb", help="debugger type, one of [gdb, lldb]")
    parser.add_argument('--args', default='', help="command line arguments if target binary")
    parser.add_argument(
        '--config',
        default=None,
        help="bazel build config (e.g., clang, gcc). Auto-detected if not specified")
    parser.add_argument(
        '--overwrite',
        action="store_true",
        help="recreate config without preserving any existing config")
    parser.add_argument('target', help="target binary which you want to build")
    args = parser.parse_args()

    # Auto-detect config if not specified
    config = args.config if args.config else auto_detect_config()
    if config:
        print(f"Using build config: {config}")
    else:
        print("Using default Bazel configuration")

    workspace = get_workspace()
    execution_root = get_execution_root(workspace)
    debug_binary = build_binary_with_debug_info(args.target, config)
    add_to_launch_json(
        args.target, debug_binary, workspace, execution_root, args.args, args.debugger,
        args.overwrite)
