#!/usr/bin/env python

# Quick-and-dirty python to fetch dependency information

import importlib
import json
import re
import subprocess
import sys

API_DEPS = importlib.machinery.SourceFileLoader('api',
                                                'api/bazel/repository_locations.bzl').load_module()
DEPS = importlib.machinery.SourceFileLoader('deps', 'bazel/repository_locations.bzl').load_module()


def print_deps(deps):
    print(json.dumps(deps, sort_keys=True, indent=2))


if __name__ == '__main__':
    deps = []

    DEPS.REPOSITORY_LOCATIONS_SPEC.update(API_DEPS.REPOSITORY_LOCATIONS_SPEC)

    for key, loc in DEPS.REPOSITORY_LOCATIONS_SPEC.items():
        deps.append({
            'identifier': key,
            'description': loc.get('project_desc'),
            'project': loc.get('project_url'),
            'version': loc.get("version"),
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
