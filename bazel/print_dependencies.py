#!/usr/bin/env python

# Quick-and-dirty python to fetch dependency information

import imp
import json
import os.path
import re

API_DEPS = imp.load_source('api', 'api/bazel/repository_locations.bzl')
DEPS = imp.load_source('deps', 'bazel/repository_locations.bzl')
RECIPES = imp.load_source('recipies', 'bazel/target_recipes.bzl')
RECIPE_VERSIONS_FILE = 'ci/build_container/build_recipes/versions.sh'


# parseRecipeDetails will extact a build_recipe version
# information from the given text. The text typically
# being the build_recipes/version.sh file
def parseRecipeDetails(name, text):
  info = {}

  # Remove dashes
  prefix = name.upper().translate(None, '-')
  scans = {
    prefix + '_FILE_SHA256': 'file-sha256',
    prefix + '_FILE_URL': 'file-url',
  }

  for env_var, json_key in scans.items():
    match = re.search(env_var + '=(.*)', text)
    if match:
      info[json_key] = expandVars(match.group(1))

  return info


# expandVars will replace references to environment
# variables in the given 'value' string
def expandVars(value):
  return os.path.expandvars(value)


if __name__ == '__main__':
  deps = []

  with open(RECIPE_VERSIONS_FILE, 'r') as raw:
    recipe_versions = raw.read()

  # Populate the environment so we may resolve
  # environment variables
  for match in re.finditer('(.*)=(.*)', recipe_versions):
    os.environ[match.group(1)] = expandVars(match.group(2))

  DEPS.REPOSITORY_LOCATIONS.update(API_DEPS.REPOSITORY_LOCATIONS)
  for key, loc in DEPS.REPOSITORY_LOCATIONS.items():
    deps.append({
      'identifier': key,
      'file-sha256': loc.get('sha256'),
      'file-url': loc.get('urls')[0],
    })

  for key, name in RECIPES.TARGET_RECIPES.items():
    info = parseRecipeDetails(name, recipe_versions)
    info['identifier'] = key
    deps.append(info)

  deps = sorted(deps, key=lambda k: k['identifier'])
  print json.dumps(deps, sort_keys=True, indent=2)
