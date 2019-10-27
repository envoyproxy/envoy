#!/usr/bin/env python3

# Enforces:
# - License headers on Envoy BUILD files
# - envoy_package() top-level invocation for standard Envoy package setup.
# - Infers API dependencies from source files.
# - Misc. cleanups: avoids redundant blank lines, removes unused loads.
# - Maybe more later?

import functools
import os
import re
import subprocess
import sys
import tempfile

# Where does Buildozer live?
BUILDOZER_PATH = os.getenv('BUILDOZER_BIN', '$GOPATH/bin/buildozer')

# Canonical Envoy license.
LICENSE_STRING = 'licenses(["notice"])  # Apache 2\n\n'

# Match any existing licenses in a BUILD file.
OLD_LICENSES_REGEX = re.compile(r'^licenses\(.*\n+', re.MULTILINE)

# Match an Envoy rule, e.g. envoy_cc_library( in a BUILD file.
ENVOY_RULE_REGEX = re.compile(r'envoy[_\w]+\(')

# Match a load() statement for the envoy_package macro.
PACKAGE_LOAD_BLOCK_REGEX = re.compile('("envoy_package".*?\)\n)', re.DOTALL)

# Match Buildozer 'print' output. Example of Buildozer print output:
# cc_library json_transcoder_filter_lib [json_transcoder_filter.cc] (missing) (missing)
BUILDOZER_PRINT_REGEX = re.compile(
    '\s*([\w_]+)\s*([\w_]+)\s*[(\[](.*?)[)\]]\s* [(\[](.*?)[)\]]\s*[(\[](.*?)[)\]]')

# Match API header include in Envoy source file?
API_INCLUDE_REGEX = re.compile('#include "(envoy/.*)/[^/]+\.pb\.(validate\.)?h"')


class EnvoyBuildFixerError(Exception):
  pass


# Run Buildozer commands on a string representing a BUILD file.
def RunBuildozer(cmds, contents):
  with tempfile.NamedTemporaryFile(mode='w') as cmd_file:
    # We send the BUILD contents to buildozer on stdin and receive the
    # transformed BUILD on stdout. The commands are provided in a file.
    cmd_input = '\n'.join('%s|-:%s' % (cmd, target) for cmd, target in cmds)
    cmd_file.write(cmd_input)
    cmd_file.flush()
    r = subprocess.run([BUILDOZER_PATH, '-stdout', '-f', cmd_file.name],
                       input=contents.encode(),
                       stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    # Buildozer uses 3 for success but no change (0 is success and changed).
    if r.returncode != 0 and r.returncode != 3:
      raise EnvoyBuildFixerError('buildozer execution failed: %s' % r)
    # Sometimes buildozer feels like returning nothing when the transform is a
    # nop.
    if not r.stdout:
      return contents
    return r.stdout.decode('utf-8')


# Add an Apache 2 license and envoy_package() import and rule as needed.
def FixPackageAndLicense(contents):
  # Ensure we have an envoy_package import load if this is a real Envoy package.
  if re.search(ENVOY_RULE_REGEX, contents):
    contents = RunBuildozer([
        ('new_load //bazel:envoy_build_system.bzl envoy_package', '__pkg__'),
    ], contents)
    # Envoy package is inserted after the load block containing the
    # envoy_package import.
    if 'envoy_package()' not in contents:
      contents = re.sub(PACKAGE_LOAD_BLOCK_REGEX, r'\1\nenvoy_package()\n\n', contents)
      if 'envoy_package()' not in contents:
        raise EnvoyBuildFixerError('Unable to insert envoy_package()')
  # Delete old licenses.
  if re.search(OLD_LICENSES_REGEX, contents):
    contents = re.sub(OLD_LICENSES_REGEX, '', contents)
  # Add canonical Apache 2 license.
  contents = LICENSE_STRING + contents
  return contents


# Remove trailing blank lines, unnecessary double blank lines.
def FixEmptyLines(contents):
  return re.sub('\n\s*$', '\n', re.sub('\n\n\n', '\n\n', contents))


def FixBuild(path):
  with open(path, 'r') as f:
    contents = f.read()
  xforms = [
      FixPackageAndLicense,
      FixEmptyLines,
  ]
  for xform in xforms:
    contents = xform(contents)
  return contents


if __name__ == '__main__':
  if len(sys.argv) == 2:
    sys.stdout.write(FixBuild(sys.argv[1]))
    sys.exit(0)
  elif len(sys.argv) == 3:
    reorderd_source = FixBuild(sys.argv[1])
    with open(sys.argv[2], 'w') as f:
      f.write(reorderd_source)
    sys.exit(0)
  print('Usage: %s <source file path> [<destination file path>]' % sys.argv[0])
  sys.exit(1)
