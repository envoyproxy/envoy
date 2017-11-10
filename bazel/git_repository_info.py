#!/usr/bin/env python

# Quick-and-dirty Python to fetch git repository info in bazel/repository_locations.bzl.

import imp
import sys
import subprocess as sp

repolocs = imp.load_source('replocs', 'bazel/repository_locations.bzl')

if __name__ == '__main__':
  if len(sys.argv) != 2:
    print 'Usage: %s <repository name>' % sys.argv[0]
    sys.exit(1)
  repo = sys.argv[1]
  if repo not in repolocs.REPOSITORY_LOCATIONS:
    print 'Unknown repository: %s' % repo
    sys.exit(1)
  repoloc = repolocs.REPOSITORY_LOCATIONS[repo]
  print '%s %s' % (repoloc['remote'], repoloc['commit']) 
