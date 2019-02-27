#!/usr/bin/env python

# Quick-and-dirty python to fetch dependency information

import imp
import json
import os.path
import re

API_DEPS = imp.load_source('api', 'api/bazel/repository_locations.bzl')
DEPS = imp.load_source('deps', 'bazel/repository_locations.bzl')
RECIPE_INFO = imp.load_source('recipes', 'ci/build_container/build_recipes/versions.py')

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

  for key, loc in RECIPE_INFO.RECIPES.items():
    deps.append({
        'identifier': key,
        'file-sha256': loc.get('sha256'),
        'file-url': loc.get('url'),
        'file-prefix': loc.get('strip_prefix', ''),
    })

  deps = sorted(deps, key=lambda k: k['identifier'])
  print json.dumps(deps, sort_keys=True, indent=2)
