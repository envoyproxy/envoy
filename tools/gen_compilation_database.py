#!/usr/bin/env python

import argparse
import os
import json

def generateCompilationDatabase():
  os.system("bazel/gen_compilation_database.sh //source/... //test/... //tools/...")

def isCompileTarget(target, args):
  filename = target["file"]
  if not args.include_headers:
    for ext in (".h", ".hh", ".hpp", ".hxx"):
      if filename.endswith(".h") or filename.endswith(ext):
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

  target["command"] = " ".join(["clang++", options])
  return target

def fixCompilationDatabase(args):
  with open("compile_commands.json", "r") as db_file:
    db = json.load(db_file)

  db = [modifyCompileCommand(target) for target in db if isCompileTarget(target, args)]
  with open("compile_commands.json", "w") as db_file:
    json.dump(db, db_file, indent=2)

if __name__ == "__main__":
  parser = argparse.ArgumentParser(description='Generate JSON compilation database')
  parser.add_argument('--include_external', type=bool, default=False)
  parser.add_argument('--include_genfiles', type=bool, default=False)
  parser.add_argument('--include_headers', type=bool, default=False)
  args = parser.parse_args()
  generateCompilationDatabase()
  fixCompilationDatabase(args)
