#!/usr/bin/env python

# Enforce license headers on Envoy BUILD files (maybe more later?)

import sys

LICENSE_STRING = 'licenses(["notice"])  # Apache 2\n'

def FixBuild(path):
  with open(path, 'r') as f:
    contents = f.read()

  if not contents.startswith(LICENSE_STRING):
    return LICENSE_STRING + contents
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
  print 'Usage: %s <source file path> [<destination file path>]' % sys.argv[0]
  sys.exit(1)
