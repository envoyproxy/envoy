#!/usr/bin/env python3

# Run OSSF Scorecard (https://github.com/ossf/scorecard) against Envoy dependencies.
#
# Usage:
#
#   export API_PATH=api/
#   tools/dependency/ossf_scorecard.py <path to repository_locations.bzl> \
#       <path to scorecard JSON results> \
#       <path to scorecard binary> \
#       <output CSV path>
#
# You will need to checkout and build the OSSF scorecard binary independently and supply it as a CLI
# argument. This will only be used it the scorecards JSON results does not contain the result.
# The JSON result is located at https://storage.cloud.google.com/ossf-scorecards/latest.json.
# TODO(asraa): Replace with bigquery.
#
# You will need to set a GitHub access token in the GITHUB_AUTH_TOKEN environment variable. You can
# generate personal access tokens under developer settings on GitHub. You should restrict the scope
# of the token to "repo: public_repo".
#
# The output is CSV suitable for import into Google Sheets.

from collections import namedtuple
from google.cloud import storage
from urllib.parse import urlparse
import re
import csv
import tempfile
import json
import os
import subprocess as sp
import sys

import tools.dependency.exports
from tools.dependency import utils

Incompatible = 'Not Scorecard compatible'
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


def format_scorecard(result):
    if result.releases == Incompatible:
        return result.releases
    res = ""
    for name, val in result._asdict().items():
        if name != 'name':
            res += name + " " + val
    return res


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


def strip_scheme(url: str):
    return re.sub(r'^https?:\/\/', '', url)


def get_json_results(projects, gcs_path):
    projects = [strip_scheme(p) for p in projects]
    # Returns a CSV result from the JSON file.
    path = urlparse(gcs_path).path
    bucket_name, blob_name = os.path.split(path)
    client = storage.Client.create_anonymous_client()
    bucket = client.bucket(bucket_name.strip("/"))
    blob = bucket.blob(blob_name)

    content = blob.download_as_string().decode('UTF-8')
    results = {}
    for line in content.splitlines():
        res = json.loads(line)
        if res['Repo'] in projects:
            results[res['Repo']] = res
    return results


def score(scorecard_results, scorecard_path, repository_locations):
    results = {}
    github_projects = {}
    for dep, metadata in sorted(repository_locations.items()):
        if not is_scored_use_category(metadata['use_category']):
            continue
        results_key = metadata['project_name']
        formatted_name = '=HYPERLINK("%s", "%s")' % (metadata['project_url'], results_key)
        github_project_url = utils.get_github_project_url(metadata['urls'])
        if not github_project_url:
            na = Incompatible
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
        github_projects[github_project_url] = metadata
    # Get filtered results from the JSON bigquery output
    json_res = get_json_results(github_projects, scorecard_results)
    for github_project_url, metadata in github_projects.items():
        if strip_scheme(github_project_url) in json_res:
            raw_scorecard = json_res[strip_scheme(github_project_url)]
        else:
            # Fall back on the scorecard binary
            if scorecard_path != None:
                raw_scorecard = json.loads(
                    sp.check_output([
                        scorecard_path, f'--repo={github_project_url}', '--show-details',
                        '--format=json'
                    ]))
            else:
                na = 'Scorecard not found'
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

        checks = {c['Name']: c for c in raw_scorecard['Checks']}

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

        results_key = metadata['project_name']
        formatted_name = '=HYPERLINK("%s", "%s")' % (metadata['project_url'], results_key)
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
    return results


def print_csv_results(csv_output_path, results):
    headers = Scorecard._fields
    with open(csv_output_path, 'w') as f:
        writer = csv.writer(f)
        writer.writerow(headers)
        for name in sorted(results):
            writer.writerow(getattr(results[name], h) for h in headers)


if __name__ == '__main__':
    if len(sys.argv) != 5:
        print(
            'Usage: %s <path to repository_locations.bzl> <path to scorecard JSON results> <path to scorecard binary> <output CSV path>'
            % sys.argv[0])
        sys.exit(1)
    access_token = os.getenv('GITHUB_AUTH_TOKEN')
    if not access_token:
        print('Missing GITHUB_AUTH_TOKEN')
        sys.exit(1)
    path = sys.argv[1]
    scorecard_json = sys.argv[2]
    scorecard_path = sys.argv[3]
    csv_output_path = sys.argv[4]
    spec_loader = exports.repository_locations_utils.load_repository_locations_spec
    path_module = exports.load_module('repository_locations', path)
    try:
        results = score(
            scorecard_json, scorecard_path, spec_loader(path_module.REPOSITORY_LOCATIONS_SPEC))
        for result in results:
            print(result)
        print_csv_results(csv_output_path, results)
    except OssfScorecardError as e:
        print(
            f'An error occurred while processing {path}, please verify the correctness of the '
            f'metadata: {e}')
