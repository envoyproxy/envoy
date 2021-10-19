""" Script for checking that PR descriptions contain required fieds."""

import datetime
import os
import re
import sys

import github


def check_pr_description(id):
    git = github.Github()
    repo = git.get_repo('envoyproxy/envoy')
    pr = repo.get_pull(id)

    valid = True
    # Check for required fields from PULL_REQUEST_TEMPLATE.md
    for field in ['Risk Level', 'Testing', 'Docs Changes', 'Release Notes',
                  'Platform Specific Features']:
        match = re.search('^\s*%s:\s*(.+)$' % field, pr.body, re.MULTILINE)
        if not match:
            print('Missing required field: %s' % field)
            valid = False
        else:
            print(match.group(1))

    if not valid:
        print(
            'Invalid PR description. Please see %s for more information.'
            % 'https://github.com/envoyproxy/envoy/blob/main/PULL_REQUESTS.md')
        sys.exit(1)


if __name__ == '__main__':
    GITHUB_PR_ID = os.getenv('GITHUB_PR_ID')
    if not GITHUB_PR_ID:
        print('GITHUB_PR_ID not set')
        sys.exit(1)

    try:
        id = int(GITHUB_PR_ID)
        check_pr_description(id)
    except ValueError:
        print('Invalid PR ID: "%s"' % GITHUB_PR_ID)
        sys.exit(2)
