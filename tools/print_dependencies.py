#!/usr/bin/env python

# Quick-and-dirty python to fetch dependency information

import imp
import json
import os.path
import re
import subprocess
import sys

API_DEPS = imp.load_source('api', 'api/bazel/repository_locations.bzl')
DEPS = imp.load_source('deps', 'bazel/repository_locations.bzl')


def print_deps(deps):
  print(json.dumps(deps, sort_keys=True, indent=2))


if __name__ == '__main__':
  deps = []

  DEPS.REPOSITORY_LOCATIONS.update(API_DEPS.REPOSITORY_LOCATIONS)

  for key, loc in DEPS.REPOSITORY_LOCATIONS.items():
    deps.append({
        'identifier': key,
        'file-sha256': loc.get('sha256'),
        'file-url': loc.get('urls')[0],
        'file-prefix': loc.get('strip_prefix', ''),
    })

  deps = sorted(deps, key=lambda k: k['identifier'])

  # Print all dependencies if a target is unspecified
  if len(sys.argv) == 1:
    print_deps(deps)
    exit(0)

  # Bazel target to print
  target = sys.argv[1]
  output = subprocess.check_output(['bazel', 'query', 'deps(%s)' % target])

  repos = set()

  # Gather the explicit list of repositories
  repo_regex = re.compile('^@(.*)\/\/')
  for line in output.split('\n'):
    match = repo_regex.match(line)
    if match:
      repos.add(match.group(1))

  deps = filter(lambda dep: dep['identifier'] in repos, deps)
  print_deps(deps)
