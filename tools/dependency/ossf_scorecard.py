#!/usr/bin/env python3

# Run OSSF Scorecard (https://github.com/ossf/scorecard) against Envoy dependencies.
#
# Usage:
#
#   export API_PATH=api/
#   tools/dependency/ossf_scorecard.py <path to repository_locations.bzl> \
#       <path to scorecard binary> \
#       <output CSV path>
#
# You will need to checkout and build the OSSF scorecard binary independently and supply it as a CLI
# argument.
#
# You will need to set a GitHub access token in the GITHUB_AUTH_TOKEN environment variable. You can
# generate personal access tokens under developer settings on GitHub. You should restrict the scope
# of the token to "repo: public_repo".
#
# The output is CSV suitable for import into Google Sheets.

from collections import namedtuple
import csv
import json
import os
import subprocess as sp
import sys
from importlib.util import spec_from_loader, module_from_spec
from importlib.machinery import SourceFileLoader

Scorecard = namedtuple(
    'Scorecard', [
        'name',
        'contributors',
        'active',
        'ci_tests',
        'pull_requests',
        'code_review',
        'fuzzing',
        'security_policy',
        'releases',
    ])


# Obtain GitHub project URL from a list of URLs.
def get_github_project_url(urls):
    for url in urls:
        if not url.startswith('https://github.com/'):
            continue
        components = url.split('/')
        return f'https://github.com/{components[3]}/{components[4]}'
    return None


# Shared Starlark/Python files must have a .bzl suffix for Starlark import, so
# we are forced to do this workaround.
def load_module(name, path):
    spec = spec_from_loader(name, SourceFileLoader(name, path))
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# this is the relative path in a bazel build
# to call this module outside of a bazel build set the `API_PATH` first,
# for example, if running from the envoy repo root: `export API_PATH=api/`
api_path = os.getenv("API_PATH", "external/envoy_api")

# Modules
repository_locations_utils = load_module(
    'repository_locations_utils', os.path.join(api_path, 'bazel/repository_locations_utils.bzl'))


# Thrown on errors related to release date.
class OssfScorecardError(Exception):
    pass


# We skip build, test, etc.
def is_scored_use_category(use_category):
    return len(
        set(use_category).intersection([
            'dataplane_core', 'dataplane_ext', 'controlplane', 'observability_core',
            'observability_ext'
        ])) > 0


def score(scorecard_path, repository_locations):
    results = {}
    for dep, metadata in sorted(repository_locations.items()):
        if not is_scored_use_category(metadata['use_category']):
            continue
        results_key = metadata['project_name']
        formatted_name = '=HYPERLINK("%s", "%s")' % (metadata['project_url'], results_key)
        github_project_url = get_github_project_url(metadata['urls'])
        if not github_project_url:
            na = 'Not Scorecard compatible'
            results[results_key] = Scorecard(
                name=formatted_name,
                contributors=na,
                active=na,
                ci_tests=na,
                pull_requests=na,
                code_review=na,
                fuzzing=na,
                security_policy=na,
                releases=na)
            continue
        raw_scorecard = json.loads(
            sp.check_output(
                [scorecard_path, f'--repo={github_project_url}', '--show-details',
                 '--format=json']))
        checks = {c['CheckName']: c for c in raw_scorecard['Checks']}

        # Generic check format.
        def _format(key):
            score = checks[key]
            status = score['Pass']
            confidence = score['Confidence']
            return f'{status} ({confidence})'

        # Releases need to be extracted from Signed-Releases.
        def release_format():
            score = checks['Signed-Releases']
            if score['Pass']:
                return _format('Signed-Releases')
            details = score['Details']
            release_found = details is not None and any('release found:' in d for d in details)
            if release_found:
                return 'True (10)'
            else:
                return 'False (10)'

        results[results_key] = Scorecard(
            name=formatted_name,
            contributors=_format('Contributors'),
            active=_format('Active'),
            ci_tests=_format('CI-Tests'),
            pull_requests=_format('Pull-Requests'),
            code_review=_format('Code-Review'),
            fuzzing=_format('Fuzzing'),
            security_policy=_format('Security-Policy'),
            releases=release_format())
        print(raw_scorecard)
        print(results[results_key])
    return results


def print_csv_results(csv_output_path, results):
    headers = Scorecard._fields
    with open(csv_output_path, 'w') as f:
        writer = csv.writer(f)
        writer.writerow(headers)
        for name in sorted(results):
            writer.writerow(getattr(results[name], h) for h in headers)


if __name__ == '__main__':
    if len(sys.argv) != 4:
        print(
            'Usage: %s <path to repository_locations.bzl> <path to scorecard binary> <output CSV path>'
            % sys.argv[0])
        sys.exit(1)
    access_token = os.getenv('GITHUB_AUTH_TOKEN')
    if not access_token:
        print('Missing GITHUB_AUTH_TOKEN')
        sys.exit(1)
    path = sys.argv[1]
    scorecard_path = sys.argv[2]
    csv_output_path = sys.argv[3]
    spec_loader = repository_locations_utils.load_repository_locations_spec
    path_module = load_module('repository_locations', path)
    try:
        results = score(scorecard_path, spec_loader(path_module.REPOSITORY_LOCATIONS_SPEC))
        print_csv_results(csv_output_path, results)
    except OssfScorecardError as e:
        print(
            f'An error occurred while processing {path}, please verify the correctness of the '
            f'metadata: {e}')
