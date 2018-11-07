#!/usr/bin/env python

import argparse
import os
import json
import subprocess


def generateCompilationDatabase(args):
  if args.run_bazel_build:
    subprocess.check_call(["bazel", "build"] + args.bazel_targets)

  gen_compilation_database_sh = os.path.join(
      os.path.realpath(os.path.dirname(__file__)), "../bazel/gen_compilation_database.sh")
  subprocess.check_call([gen_compilation_database_sh] + args.bazel_targets)


def isHeader(filename):
  for ext in (".h", ".hh", ".hpp", ".hxx"):
    if filename.endswith(ext):
      return True
  return False


def isCompileTarget(target, args):
  filename = target["file"]
  if not args.include_headers and isHeader(filename):
    return False

  if not args.include_genfiles:
    if filename.startswith("bazel-out/"):
      return False

  if not args.include_external:
    if filename.startswith("external/"):
      return False

  return True


def modifyCompileCommand(target):
  cxx, options = target["command"].split(" ", 1)

  # Workaround for bazel added C++11 options, those doesn't affect build itself but
  # clang-tidy will misinterpret them.
  options = options.replace("-std=c++0x ", "")
  options = options.replace("-std=c++11 ", "")

  if isHeader(target["file"]):
    options += " -Wno-pragma-once-outside-header -Wno-unused-const-variable"
    options += " -Wno-unused-function"

  target["command"] = " ".join(["clang++", options])
  return target


def fixCompilationDatabase(args):
  with open("compile_commands.json", "r") as db_file:
    db = json.load(db_file)

  db = [modifyCompileCommand(target) for target in db if isCompileTarget(target, args)]

  # Remove to avoid writing into symlink
  os.remove("compile_commands.json")
  with open("compile_commands.json", "w") as db_file:
    json.dump(db, db_file, indent=2)


if __name__ == "__main__":
  parser = argparse.ArgumentParser(description='Generate JSON compilation database')
  parser.add_argument('--run_bazel_build', action='store_true')
  parser.add_argument('--include_external', action='store_true')
  parser.add_argument('--include_genfiles', action='store_true')
  parser.add_argument('--include_headers', action='store_true')
  parser.add_argument(
      'bazel_targets', nargs='*', default=["//source/...", "//test/...", "//tools/..."])
  args = parser.parse_args()
  generateCompilationDatabase(args)
  fixCompilationDatabase(args)
