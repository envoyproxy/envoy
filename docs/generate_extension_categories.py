#!/usr/bin/env python3

# Generate CSV file with names of build in extension categories.
# Category names are 'grepped' from source files by looking
# for the "static const char FACTORY_CATEGORY[] = {"foo.bar"};"
# declarations.

import os
import pathlib
import subprocess
import sys


def GetCurrentCommitSHA():
  r = subprocess.run(
      ['git', 'rev-list', 'HEAD', '-n', '1'],
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE)

  commit_sha = r.stdout.decode('utf-8').strip()
  return commit_sha


if __name__ == '__main__':
  output_file_path = os.path.join(sys.argv[1], 'configuration', 'factory_categories.csv')
  commit_sha = GetCurrentCommitSHA()

  r = subprocess.run(
      ['find . -name *.h -exec grep -HP "static\s+const\s+char\s+FACTORY_CATEGORY\[\s*\]\s*=\s*\{\s*\".*\"\s*\}\s*;" {} \;'],
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE,
      shell=True)

  lines = r.stdout.decode('utf-8').strip().split('\n')
  output = '"Category Name", "Source File"\n'
  for line in lines:
    # Split the file path from the declaration separated by the ':'
    file_path, declaration = line.split(':')
    # The file path always starts with the "./" which we remove here for readability.
    file_path = file_path[2:]
    # The category name is the quoted string in the declaration. Tease it out.
    prefix, category_name, suffix = declaration.split('"')
    output += '"%s", "`%s <https://github.com/envoyproxy/envoy/blob/%s/%s>`_"\n' % (category_name, file_path, commit_sha, file_path)

  pathlib.Path(output_file_path).write_text(output)
