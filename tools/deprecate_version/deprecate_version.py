# Script for automating cleanup PR creation for deprecated runtime features
#
# sh tools/deprecate_version/deprecate_version.sh
#
# Direct usage (not recommended):
#
# python tools/deprecate_version/deprecate_version.py
#
# e.g
#
#  python tools/deprecate_version/deprecate_version.py
#
# A GitHub access token must be set in GH_ACCESS_TOKEN. To create one, go to
# Settings -> Developer settings -> Personal access tokens in GitHub and create
# a token with public_repo scope. Keep this safe, it's broader than it needs to
# be thanks to GH permission model
# (https://github.com/dear-github/dear-github/issues/113).
#
# Known issues:
# - Minor fixup PRs (e.g. fixing a typo) will result in the creation of spurious
#   issues.

from __future__ import print_function

from collections import defaultdict
import os
import re
import subprocess
import sys

import github
from git import Repo

try:
  input = raw_input  # Python 2
except NameError:
  pass  # Python 3

# Tag issues created with these labels.
LABELS = ['deprecation', 'tech debt', 'no stalebot']


# Errors that happen during issue creation.
class DeprecateVersionError(Exception):
  pass


# Figure out map from version to set of commits.
def GetHistory():
  """Obtain mapping from release version to docs/root/intro/deprecated.rst PRs.

  Returns:
    A dictionary mapping from release version to a set of git commit objects.
  """
  repo = Repo(os.getcwd())
  version = None
  history = defaultdict(set)
  for commit, lines in repo.blame('HEAD', 'docs/root/intro/deprecated.rst'):
    for line in lines:
      sr = re.match('## Version (.*) \(.*\)', line)
      if sr:
        version = sr.group(1)
        continue
      history[version].add(commit)
  return history


def GetConfirmation():
  """Obtain stdin confirmation to create issues in GH."""
  return input('Creates issues? [yN] ').strip().lower() in ('y', 'yes')


def CreateIssues(access_token, runtime_and_pr):
  """Create issues in GitHub for code to clean up old runtime guarded features.

  Args:
    access_token: GitHub access token (see comment at top of file).
    runtime_and_pr: a list of runtime guards and the PRs they were added.
  """
  repo = github.Github(access_token).get_repo('envoyproxy/envoy')

  # Find GitHub label objects for LABELS.
  labels = []
  for label in repo.get_labels():
    if label.name in LABELS:
      labels.append(label)
  if len(labels) != len(LABELS):
    raise DeprecateVersionError('Unknown labels (expected %s, got %s)' % (LABELS, labels))

  issues = []
  for runtime_guard, pr in runtime_and_pr:
    # Who is the author?
    pr_info = repo.get_pull(pr)

    title = '%s deprecation' % (runtime_guard)
    body = ('#%d (%s) introduced a runtime guarded feature. This issue '
            'tracks source code cleanup.') % (pr, pr_info.title)
    print(title)
    print(body)
    print('  >> Assigning to %s' % pr_info.user.login)

    # TODO(htuch): Figure out how to do this without legacy and faster.
    exists = repo.legacy_search_issues('open', '"%s"' % title) or repo.legacy_search_issues(
        'closed', '"%s"' % title)
    if exists:
      print('  >> Issue already exists, not posting!')
    else:
      issues.append((title, body, pr_info.user))

  if not issues:
    print('No features to deprecate in this release')
    return

  if GetConfirmation():
    print('Creating issues...')
    for title, body, user in issues:
      try:
        repo.create_issue(title, body=body, assignees=[user.login], labels=labels)
      except github.GithubException as e:
        print(('GithubException while creating issue. This is typically because'
               ' a user is not a member of envoyproxy org. Check that %s is in '
               'the org.') % user.login)
        raise


def GetRuntimeAlreadyTrue():
  """Returns a list of runtime flags already defaulted to true
  """
  runtime_already_true = []
  runtime_features = re.compile(r'.*"(envoy.reloadable_features..*)",.*')
  with open('source/common/runtime/runtime_features.cc', 'r') as features:
    for line in features.readlines():
      match = runtime_features.match(line)
      if match and 'test_feature_true' not in match.group(1):
        print("Found existing flag " + match.group(1))
        runtime_already_true.append(match.group(1))

  return runtime_already_true


def GetRuntimeAndPr():
  """Returns a list of tuples of [runtime features to deprecate, PR the feature was added]
  """
  repo = Repo(os.getcwd())

  runtime_already_true = GetRuntimeAlreadyTrue()

  # grep source code looking for reloadable features which are true to find the
  # PR they were added.
  grep_output = subprocess.check_output('grep -r "envoy.reloadable_features\." source/', shell=True)
  features_to_flip = []
  runtime_feature_regex = re.compile(r'.*(source.*cc).*"(envoy.reloadable_features\.[^"]+)".*')
  for line in grep_output.splitlines():
    match = runtime_feature_regex.match(str(line))
    if match:
      filename = (match.group(1))
      runtime_guard = match.group(2)
      # If this runtime guard isn't true, ignore it for this release.
      if not runtime_guard in runtime_already_true:
        continue
      # For true runtime guards, walk the blame of the file they were added to,
      # to find the pr the feature was added.
      for commit, lines in repo.blame('HEAD', filename):
        for line in lines:
          if runtime_guard in line:
            pr = (int(re.search('\(#(\d+)\)', commit.message).group(1)))
            # Add the runtime guard and PR to the list to file issues about.
            features_to_flip.append((runtime_guard, pr))

    else:
      print('no match in ' + str(line) + ' please address manually!')

  return features_to_flip


if __name__ == '__main__':
  runtime_and_pr = GetRuntimeAndPr()

  access_token = os.getenv('GH_ACCESS_TOKEN')
  if not access_token:
    print('Missing GH_ACCESS_TOKEN')
    sys.exit(1)

  CreateIssues(access_token, runtime_and_pr)
