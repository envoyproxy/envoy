#!/usr/bin/env python3

import argparse
import json
import os
import pathlib
import shlex
import shutil
import subprocess

BAZEL_OPTIONS = shlex.split(os.environ.get("BAZEL_BUILD_OPTIONS", ""))


def bazelInfo(name, bazel_extra_options=[]):
  return subprocess.check_output(["bazel", "info", name] + BAZEL_OPTIONS +
                                 bazel_extra_options).decode().strip()


def getWorkspace():
  return bazelInfo("workspace")


def getExecutionRoot(workspace):
  # If compilation database exists, use its execution root, this allows setting
  # breakpoints with clangd navigation easier.
  try:
    compdb = pathlib.Path(workspace, "compile_commands.json").read_text()
    return json.loads(compdb)[0]['directory']
  except:
    return bazelInfo("execution_root")


def binaryPath(bazel_bin, target):
  return pathlib.Path(
      bazel_bin,
      *[s for s in target.replace('@', 'external/').replace(':', '/').split('/') if s != ''])


def buildBinaryWithDebugInfo(target):
  targets = [target, target + ".dwp"]
  subprocess.check_call(["bazel", "build", "-c", "dbg"] + BAZEL_OPTIONS + targets)

  bazel_bin = bazelInfo("bazel-bin", ["-c", "dbg"])
  return binaryPath(bazel_bin, target)


def getLaunchJson(workspace):
  try:
    return json.loads(pathlib.Path(workspace, ".vscode", "launch.json").read_text())
  except:
    return {"version": "0.2.0"}


def writeLaunchJson(workspace, launch):
  launch_json = pathlib.Path(workspace, ".vscode", "launch.json")
  backup_launch_json = pathlib.Path(workspace, ".vscode", "launch.json.bak")
  if launch_json.exists():
    shutil.move(str(launch_json), str(backup_launch_json))

  launch_json.write_text(json.dumps(launch, indent=4))


def gdbConfig(target, binary, workspace, execroot, arguments):
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


def lldbConfig(target, binary, workspace, execroot, arguments):
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


def addToLaunchJson(target, binary, workspace, execroot, arguments, debugger_type):
  launch = getLaunchJson(workspace)
  new_config = {}
  if debugger_type == "lldb":
    new_config = lldbConfig(target, binary, workspace, execroot, arguments)
  else:
    new_config = gdbConfig(target, binary, workspace, execroot, arguments)

  configurations = launch.get("configurations", [])
  for config in configurations:
    if config.get("name", None) == new_config["name"]:
      config.clear()
      config.update(new_config)
      break
  else:
    configurations.append(new_config)

  launch["configurations"] = configurations
  writeLaunchJson(workspace, launch)


if __name__ == "__main__":
  parser = argparse.ArgumentParser(description='Build and generate launch config for VSCode')
  parser.add_argument('--debugger', default="gdb")
  parser.add_argument('--args', default='')
  parser.add_argument('target')
  args = parser.parse_args()

  workspace = getWorkspace()
  execution_root = getExecutionRoot(workspace)
  debug_binary = buildBinaryWithDebugInfo(args.target)
  addToLaunchJson(args.target, debug_binary, workspace, execution_root, args.args, args.debugger)
