#!/usr/bin/env python

# Enforce license headers on Envoy BUILD files (maybe more later?)

import sys

LICENSE_STRING = 'licenses(["notice"])  # Apache 2\n'
ENVOY_PACKAGE_STRING = (
    'load("//bazel:envoy_build_system.bzl", "envoy_package")\n'
    '\n'
    'envoy_package()\n')


def FixBuild(path):
  with open(path, 'r') as f:
    outlines = [LICENSE_STRING]
    load = 0
    seen_ebs = 0
    seen_epkg = 0
    for line in f:
      if line.startswith('package(') and not path.endswith(
          'bazel/BUILD') and not path.endswith('ci/prebuilt/BUILD'):
        continue
      if load == 0 and line.startswith('load('):
        load = 1
      if load == 1:
        if 'envoy_build_system.bzl' in line:
          seen_ebs = 1
        if 'envoy_package' in line:
          seen_epkg = 1
        if line.strip().endswith(')'):
          load = 2
          if seen_ebs:
            if not seen_epkg:
              outlines.append(line.strip()[:-1] + ', "envoy_package")\n')
              outlines.append('\nenvoy_package()\n')
              continue
          else:
            outlines.append(line)
            outlines.append(ENVOY_PACKAGE_STRING)
            continue
      if not line.startswith('licenses'):
        outlines.append(line)

  return ''.join(outlines)


if __name__ == '__main__':
  if len(sys.argv) == 2:
    sys.stdout.write(FixBuild(sys.argv[1]))
    sys.exit(0)
  elif len(sys.argv) == 3:
    reorderd_source = FixBuild(sys.argv[1])
    with open(sys.argv[2], 'w') as f:
      f.write(reorderd_source)
    sys.exit(0)
  print 'Usage: %s <source file path> [<destination file path>]' % sys.argv[0]
  sys.exit(1)
