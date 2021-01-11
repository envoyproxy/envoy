#!/usr/bin/env python3

# Run OSSF Scorecard (https://github.com/ossf/scorecard) against Envoy dependencies.
#
# Usage:
#
#   tools/dependency/ossf_scorecard.sh <path to repository_locations.bzl> \
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

import utils

Scorecard = namedtuple('Scorecard', [
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


# Thrown on errors related to release date.
class OssfScorecardError(Exception):
  pass


# We skip build, test, etc.
def IsScoredUseCategory(use_category):
  return len(
      set(use_category).intersection([
          'dataplane_core', 'dataplane_ext', 'controlplane', 'observability_core',
          'observability_ext'
      ])) > 0


def Score(scorecard_path, repository_locations):
  results = {}
  for dep, metadata in sorted(repository_locations.items()):
    if not IsScoredUseCategory(metadata['use_category']):
      continue
    results_key = metadata['project_name']
    formatted_name = '=HYPERLINK("%s", "%s")' % (metadata['project_url'], results_key)
    github_project_url = utils.GetGitHubProjectUrl(metadata['urls'])
    if not github_project_url:
      na = 'Not Scorecard compatible'
      results[results_key] = Scorecard(name=formatted_name,
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
            [scorecard_path, f'--repo={github_project_url}', '--show-details', '--format=json']))
    checks = {c['CheckName']: c for c in raw_scorecard['Checks']}

    # Generic check format.
    def Format(key):
      score = checks[key]
      status = score['Pass']
      confidence = score['Confidence']
      return f'{status} ({confidence})'

    # Releases need to be extracted from Signed-Releases.
    def ReleaseFormat():
      score = checks['Signed-Releases']
      if score['Pass']:
        return Format('Signed-Releases')
      details = score['Details']
      release_found = details is not None and any('release found:' in d for d in details)
      if release_found:
        return 'True (10)'
      else:
        return 'False (10)'

    results[results_key] = Scorecard(name=formatted_name,
                                     contributors=Format('Contributors'),
                                     active=Format('Active'),
                                     ci_tests=Format('CI-Tests'),
                                     pull_requests=Format('Pull-Requests'),
                                     code_review=Format('Code-Review'),
                                     fuzzing=Format('Fuzzing'),
                                     security_policy=Format('Security-Policy'),
                                     releases=ReleaseFormat())
    print(raw_scorecard)
    print(results[results_key])
  return results


def PrintCsvResults(csv_output_path, results):
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
  spec_loader = utils.repository_locations_utils.load_repository_locations_spec
  path_module = utils.LoadModule('repository_locations', path)
  try:
    results = Score(scorecard_path, spec_loader(path_module.REPOSITORY_LOCATIONS_SPEC))
    PrintCsvResults(csv_output_path, results)
  except OssfScorecardError as e:
    print(f'An error occurred while processing {path}, please verify the correctness of the '
          f'metadata: {e}')
