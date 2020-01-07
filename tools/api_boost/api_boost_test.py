#!/usr/bin/env python3

# Golden C++ source tests for API boosting. This is effectively a test for the
# combination of api_boost.py, the Clang libtooling-based
# tools/clang_tools/api_booster, as well as the type whisperer and API type
# database.

import argparse
from collections import namedtuple
import logging
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

import api_boost

TestCase = namedtuple('TestCase', ['name', 'description'])

# List of test in the form [(file_name, explanation)]
TESTS = list(
    map(lambda x: TestCase(*x), [
        ('deprecate', 'Deprecations'),
        ('elaborated_type', 'ElaboratedTypeLoc type upgrades'),
        ('using_decl', 'UsingDecl upgrades for named types'),
        ('rename', 'Annotation-based renaming'),
        ('decl_ref_expr', 'DeclRefExpr upgrades for named constants'),
        ('no_boost_file', 'API_NO_BOOST_FILE annotations'),
        ('validate', 'Validation proto header inference'),
    ]))

TESTDATA_PATH = 'tools/api_boost/testdata'


def Diff(some_path, other_path):
  result = subprocess.run(['diff', '-u', some_path, other_path], capture_output=True)
  if result.returncode == 0:
    return None
  return result.stdout.decode('utf-8') + result.stderr.decode('utf-8')


if __name__ == '__main__':
  parser = argparse.ArgumentParser(description='Golden C++ source tests for api_boost.py')
  parser.add_argument('tests', nargs='*')
  args = parser.parse_args()

  # Accumulated error messages.
  logging.basicConfig(format='%(message)s')
  messages = []

  def ShouldRunTest(test_name):
    return len(args.tests) == 0 or test_name in args.tests

  # Run API booster against test artifacts in a directory relative to workspace.
  # We use a temporary copy as the API booster does in-place rewriting.
  with tempfile.TemporaryDirectory(dir=pathlib.Path.cwd()) as path:
    # Setup temporary tree.
    shutil.copy(os.path.join(TESTDATA_PATH, 'BUILD'), path)
    for test in TESTS:
      if ShouldRunTest(test.name):
        shutil.copy(os.path.join(TESTDATA_PATH, test.name + '.cc'), path)
      else:
        # Place an empty file to make Bazel happy.
        pathlib.Path(path, test.name + '.cc').write_text('')

    # Run API booster.
    relpath_to_testdata = str(pathlib.Path(path).relative_to(pathlib.Path.cwd()))
    api_boost.ApiBoostTree([
        os.path.join(relpath_to_testdata, test.name) for test in TESTS if ShouldRunTest(test.name)
    ],
                           generate_compilation_database=True,
                           build_api_booster=True,
                           debug_log=True,
                           sequential=True)

    # Validate output against golden files.
    for test in TESTS:
      if ShouldRunTest(test.name):
        delta = Diff(os.path.join(TESTDATA_PATH, test.name + '.cc.gold'),
                     os.path.join(path, test.name + '.cc'))
        if delta is not None:
          messages.append('Non-empty diff for %s (%s):\n%s\n' %
                          (test.name, test.description, delta))

  if len(messages) > 0:
    logging.error('FAILED:\n{}'.format('\n'.join(messages)))
    sys.exit(1)
  logging.warning('PASS')
