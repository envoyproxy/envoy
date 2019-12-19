#!/usr/bin/env python3

# Golden C++ source tests for API boosting. This is effectively a test for the
# combination of api_boost.py, the Clang libtooling-based
# tools/clang_tools/api_booster, as well as the type whisperer and API type
# database.

import logging
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

import api_boost

# List of test in the form [(file_name, explanation)]
TESTS = [
    ('elaborated_type.cc', 'ElaboratedTypeLoc type upgrades'),
]

TESTDATA_PATH = 'tools/api_boost/testdata'


def Diff(some_path, other_path):
  result = subprocess.run(['diff', '-u', some_path, other_path], capture_output=True)
  if result.returncode == 0:
    return None
  return result.stdout.decode('utf-8') + result.stderr.decode('utf-8')


if __name__ == '__main__':
  # Accumulated error messages.
  logging.basicConfig(format='%(message)s')
  messages = []

  # Run API booster against test artifacts in a directory relative to workspace.
  # We use a temporary copy as the API booster does in-place rewriting.
  with tempfile.TemporaryDirectory(dir=pathlib.Path.cwd()) as path:
    # Setup temporary tree.
    shutil.copy(os.path.join(TESTDATA_PATH, 'BUILD'), path)
    for filename, _ in TESTS:
      shutil.copy(os.path.join(TESTDATA_PATH, filename), path)

    # Run API booster.
    api_boost.ApiBoostTree([str(pathlib.Path(path).relative_to(pathlib.Path.cwd()))],
                           generate_compilation_database=True,
                           build_api_booster=True,
                           debug_log=True)

    # Validate output against golden files.
    for filename, description in TESTS:
      delta = Diff(os.path.join(TESTDATA_PATH, filename + '.gold'), os.path.join(path, filename))
      if delta is not None:
        messages.append('Non-empty diff for %s (%s):\n%s\n' % (filename, description, delta))

  if len(messages) > 0:
    logging.error('FAILED:\n{}'.format('\n'.join(messages)))
    sys.exit(1)
  logging.warning('PASS')
