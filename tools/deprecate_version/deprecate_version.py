# Script for automating cleanup PR creation for deprecated features at a given
# version. Usage:
#
# tools/deprecate_version.py <deprecate for version> <deprecate by version>
#
# e.g
#
# tools/deprecate_version.py 1.6.0 1.7.0
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
#
# TODO(htuch): This is just a proof-of-concept, if we think this is worth
# pursuing, will cleanup style, comments, docstrings, error handling, etc.

from collections import defaultdict
import os
import re
import sys

import github
from git import Repo

# Tag issues created with these labels.
LABELS = ['deprecation', 'tech debt']


# Figure out map from version to set of commits.
def GetHistory():
  repo = Repo(os.getcwd())
  version = None
  history = defaultdict(set)
  for commit, lines in repo.blame('HEAD', 'DEPRECATED.md'):
    for line in lines:
      sr = re.match('## Version (.*)', line)
      if sr:
        version = sr.group(1)
        continue
      history[version].add(commit)
  return history


def GetConfirmation():
  inp = raw_input('Creates issues? [yN] ')
  if inp == 'y':
    return True
  return False


# For each commit, figure out who was behind it and file an issue if it doesn't
# exist.
def CreateIssues(deprecate_for_version, deprecate_by_version, access_token,
                 commits):
  repo = github.Github(access_token).get_repo('envoyproxy/envoy')
  # Find milestone for deprecation target.
  milestone = None
  for m in repo.get_milestones():
    if m.title == deprecate_by_version:
      milestone = m
      break
  assert (milestone)
  labels = []
  for label in repo.get_labels():
    if label.name in LABELS:
      labels.append(label)
  assert (len(labels) == len(LABELS))
  # What are the PRs corresponding to the commits?
  prs = (int(re.search('\(#(\d+)\)', c.message).group(1)) for c in commits)
  issues = []
  for pr in sorted(prs):
    # Who is the author?
    pr_info = repo.get_pull(pr)
    #print '%d %s [%s]' % (pr, pr_info.user, pr_info.title)
    title = '[v%s deprecation] Remove features marked deprecated in #%d' % (
        deprecate_for_version, pr)
    body = ('#%d (%s) introduced a deprecation notice for v%s. This issue '
            'tracks source code cleanup.') % (pr, pr_info.title,
                                              deprecate_for_version)
    print title
    print body
    print '  >> Assigning to %s' % pr_info.user.login
    # TODO(htuch): Figure out how to do this without legacy and faster.
    exists = repo.legacy_search_issues(
        'open', '"%s"' % title) or repo.legacy_search_issues(
            'closed', '"%s"' % title)
    if exists:
      print '  >> Issue already exists, not posting!'
    else:
      issues.append((title, body, pr_info.user))
  if GetConfirmation():
    print 'Creating issues...'
    for title, body, user in issues:
      try:
        repo.create_issue(
            title,
            body=body,
            assignees=[user.login],
            milestone=milestone,
            labels=labels)
      except github.GithubException:
        # TODO(htuch): Fix error handling here, should only do this if it's due
        # to a missing user in envoyproxy (could also validate earlier).
        repo.create_issue(
            title, body=body, assignees=[], milestone=milestone, labels=labels)


if __name__ == '__main__':
  if len(sys.argv) != 3:
    print 'Usage: %s <deprecate for version> <deprecate by version>' % sys.argv[
        0]
    sys.exit(1)
  access_token = os.getenv('GH_ACCESS_TOKEN')
  if not access_token:
    print 'Missing GH_ACCESS_TOKEN'
    sys.exit(1)
  deprecate_for_version = sys.argv[1]
  deprecate_by_version = sys.argv[2]
  history = GetHistory()
  if deprecate_for_version not in history:
    print 'Unknown version: %s (valid versions: %s)' % (deprecate_for_version,
                                                        history.keys())
  CreateIssues(deprecate_for_version, deprecate_by_version, access_token,
               history[deprecate_for_version])
