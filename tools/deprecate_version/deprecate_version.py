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
# - Later PRs can clobber earlier changed to DEPRECATED.md, meaning we miss
#   issues.

from collections import defaultdict
import os
import re
import sys

import github
from git import Repo

# Tag issues created with these labels.
LABELS = ['deprecation', 'tech debt']


# Errors that happen during issue creation.
class DeprecateVersionError(Exception):
  pass


# Figure out map from version to set of commits.
def GetHistory():
  """Obtain mapping from release version to DEPRECATED.md PRs.

  Returns:
    A dictionary mapping from release version to a set of git commit objects.
  """
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
  """Obtain stdin confirmation to create issues in GH."""
  inp = raw_input('Creates issues? [yN] ')
  if inp == 'y':
    return True
  return False


def CreateIssues(deprecate_for_version, deprecate_by_version, access_token,
                 commits):
  """Create issues in GitHub corresponding to a set of commits.

  Args:
    deprecate_for_version: string providing version to deprecate for, e.g.
      1.6.0.
    deprecate_by_version: string providing version to deprecate by, e.g. 1.7.0.
    access_token: GitHub access token (see comment at top of file).
    commits: set of git commit objects.
  """
  repo = github.Github(access_token).get_repo('envoyproxy/envoy')
  # Find GitHub milestone object for deprecation target.
  milestone = None
  for m in repo.get_milestones():
    if m.title == deprecate_by_version:
      milestone = m
      break
  if not milestone:
    raise DeprecateVersionError('Unknown milestone %s' % deprecate_by_version)
  # Find GitHub label objects for LABELS.
  labels = []
  for label in repo.get_labels():
    if label.name in LABELS:
      labels.append(label)
  if len(labels) != len(LABELS):
    raise DeprecateVersionError(
        'Unknown labels (expected %s, got %s)' % (LABELS, labels))
  # What are the PRs corresponding to the commits?
  prs = (int(re.search('\(#(\d+)\)', c.message).group(1)) for c in commits)
  issues = []
  for pr in sorted(prs):
    # Who is the author?
    pr_info = repo.get_pull(pr)
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
      except github.GithubException as e:
        print ('GithubException while creating issue. This is typically because'
               ' a user is not a member of envoyproxy org. Check that %s is in '
               'the org.') % user.login
        raise


if __name__ == '__main__':
  if len(sys.argv) != 3:
    print 'Usage: %s <deprecate for version> <deprecate by version>' % sys.argv[0]
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
