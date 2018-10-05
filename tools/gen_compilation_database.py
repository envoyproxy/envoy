#!/usr/bin/env python

import argparse
import os
import json

def generateCompilationDatabase():
  os.system("bazel/gen_compilation_database.sh //source/... //test/...")

def isCompileTarget(command, args):
  filename = command["file"]
  if not args.include_headers:
    if filename.endswith(".h") or filename.endswith(".hpp"):
      return False

  if not args.include_genfiles:
    if filename.startswith("bazel-out/"):
      return False

  if not args.include_external:
    if filename.startswith("external/"):
      return False

  return True

def fixCompilationDatabase(args):
  with open("compile_commands.json", "r") as db_file:
    db = json.load(db_file)

  db = [command for command in db if isCompileTarget(command, args)]
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
