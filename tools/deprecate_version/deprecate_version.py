# Bazel usage
#
# bazel run //tools/deprecate_version:deprecate_version
#
# A GitHub access token must be set in GITHUB_TOKEN. To create one, go to
# Settings -> Developer settings -> Personal access tokens in GitHub and create
# a token with public_repo scope. Keep this safe, it's broader than it needs to
# be thanks to GH permission model
# (https://github.com/dear-github/dear-github/issues/113).
#
# Known issues:
# - Minor fixup PRs (e.g. fixing a typo) will result in the creation of spurious
#   issues.

from __future__ import print_function

import datetime
from datetime import date
import os
import re
import sys

import github
from git import Repo

import envoy_repo

try:
    input = raw_input  # Python 2
except NameError:
    pass  # Python 3

# Tag issues created with these labels.
LABELS = ['deprecation', 'tech debt', 'no stalebot']


# Errors that happen during issue creation.
class DeprecateVersionError(Exception):
    pass


def get_confirmation():
    """Obtain stdin confirmation to create issues in GH."""
    return input('Creates issues? [yN] ').strip().lower() in ('y', 'yes')


def create_issues(access_token, runtime_and_pr):
    """Create issues in GitHub for code to clean up old runtime guarded features.

    Args:
        access_token: GitHub access token (see comment at top of file).
        runtime_and_pr: a list of runtime guards and the PRs and commits they were added.
    """
    git = github.Github(access_token)
    repo = git.get_repo('envoyproxy/envoy')

    # Find GitHub label objects for LABELS.
    labels = []
    for label in repo.get_labels():
        if label.name in LABELS:
            labels.append(label)
    if len(labels) != len(LABELS):
        raise DeprecateVersionError('Unknown labels (expected %s, got %s)' % (LABELS, labels))

    issues = []
    for runtime_guard, pr, commit in runtime_and_pr:
        # Who is the author?
        login = None
        if pr:
            # Extract PR title, number, and author.
            number = ('#%d') % pr
            try:
                pr_info = repo.get_pull(pr)
                change_title = pr_info.title
                login = pr_info.user.login
            except github.GithubException:
                print("Failed to fetch info for %s, trying backup method", number)

        if not login:
            # Extract commit message, sha, and author.
            # Only keep commit message title (remove description), and truncate to 50 characters.
            change_title = commit.message.split('\n')[0][:50]
            number = ('commit %s') % commit.hexsha
            email = commit.author.email
            # Use the commit author's email to search through users for their login.
            search_user = git.search_users(email.split('@')[0] + " in:email")
            login = search_user[0].login if search_user and search_user.totalCount else None

        title = '%s deprecation' % (runtime_guard)
        body = (
            'Your change %s (%s) introduced a runtime guarded feature. It has been 6 months since '
            'the new code has been exercised by default, so it\'s time to remove the old code '
            'path. This issue tracks source code cleanup so we don\'t forget.') % (
                number, change_title)

        print(title)
        print(body)
        print('  >> Assigning to %s' % (login or email))
        search_title = '%s in:title' % title

        # TODO(htuch): Figure out how to do this without legacy and faster.
        exists = repo.legacy_search_issues('open', search_title) or repo.legacy_search_issues(
            'closed', search_title)
        if exists:
            print("Issue with %s already exists" % search_title)
            print(exists)
            print('  >> Issue already exists, not posting!')
        else:
            issues.append((title, body, login))

    if not issues:
        print('No features to deprecate in this release')
        return

    if get_confirmation():
        print('Creating issues...')
        for title, body, login in issues:
            issue_created = False
            try:
                # for setec backports, we may not find a user, which would make
                # create_issue crash.
                if login:
                    repo.create_issue(title, body=body, assignees=[login], labels=labels)
                    issue_created = True
            except github.GithubException as e:
                print((
                    'unable to assign issue %s to %s. Add them to the Envoy proxy org'
                    'and assign it their way.') % (title, login))

            if not issue_created:
                try:
                    if login:
                        body += '\ncc @' + login
                    repo.create_issue(title, body=body, labels=labels)
                except github.GithubException as e:
                    print('GithubException while creating issue.')
                    raise


def get_runtime_and_pr():
    """Returns a list of tuples of [runtime features to deprecate, PR, commit the feature was added]
    """
    repo = Repo(envoy_repo.PATH)

    # grep source code looking for reloadable features which are true to find the
    # PR they were added.
    features_to_flip = []

    runtime_features = re.compile(r'.*RUNTIME_GUARD.(envoy_(reloadable|restart)_features_.*).;')

    removal_date = date.today() - datetime.timedelta(days=183)
    found_test_feature_true = False

    # Walk the blame of runtime_features and look for true runtime features older than 6 months.
    # Ignore #19880, the PR where we migrated from old style to new style flags as it
    # shouldn't change deprecation dates or ownership.
    for commit, lines in repo.blame(rev='HEAD', file='source/common/runtime/runtime_features.cc',
                                    **{"ignore-rev": "93cd7c7835a"}):
        for line in lines:
            match = runtime_features.match(line)
            if match:
                runtime_guard = match.group(1)
                if runtime_guard == 'envoy_reloadable_features_test_feature_false':
                    print("Found end sentinel\n")
                    if not found_test_feature_true:
                        # The script depends on the cc file having the true runtime block
                        # before the false runtime block.  Fail if one isn't found.
                        print('Failed to find test_feature_true.  Script needs fixing')
                        sys.exit(1)
                    return features_to_flip
                if runtime_guard == 'envoy_reloadable_features_test_feature_true':
                    found_test_feature_true = True
                    continue
                pr_num = re.search('\(#(\d+)\)', commit.message)
                # Some commits may not come from a PR (if they are part of a security point release).
                pr = (int(pr_num.group(1))) if pr_num else None
                pr_date = date.fromtimestamp(commit.committed_date)
                removable = (pr_date < removal_date)
                # Add the runtime guard and PR to the list to file issues about.
                print(
                    'Flag ' + runtime_guard + ' added at ' + str(pr_date) + ' '
                    + (removable and 'and is safe to remove' or 'is not ready to remove'))
                if removable:
                    features_to_flip.append((runtime_guard, pr, commit))
    print('Failed to find test_feature_false.  Script needs fixing')
    sys.exit(1)


if __name__ == '__main__':
    runtime_and_pr = get_runtime_and_pr()

    if not runtime_and_pr:
        print('No code is deprecated.')
        sys.exit(0)

    access_token = os.getenv('GITHUB_TOKEN')
    if not access_token:
        print(
            'Missing GITHUB_TOKEN: see instructions in tools/deprecate_version/deprecate_version.py'
        )
        sys.exit(1)

    create_issues(access_token, runtime_and_pr)
