# CLI tool to query GitHub API and compare with repository_locations.bzl. It:
# - Computes the release date of dependencies and fails if there is a mismatch
#   with the metdata release date.
# - Looks up the latest release tag on GitHub and warns if this is later than the
#   dependency version in the .bzl.
#
# Usage:
#   tools/dependency/release_dates.sh <path to repository_locations.bzl>
#
# You will need to set a GitHub access token in the GITHUB_TOKEN environment
# variable. You can generate personal access tokens under developer settings on
# GitHub. You should restrict the scope of the token to "repo: public_repo".

import os
import sys

import github

import utils


# Thrown on errors related to release date.
class ReleaseDateError(Exception):
  pass


# Format a datetime object as UTC YYYY-MM-DD.
def FormatUtcDate(date):
  # We only handle naive datetime objects right now, which is what PyGithub
  # appears to be handing us.
  assert (date.tzinfo is None)
  return date.date().isoformat()


# Obtain latest release version and compare against metadata version, warn on
# mismatch.
def VerifyAndPrintLatestRelease(dep, repo, metadata_version, release_date):
  try:
    latest_release = repo.get_latest_release()
    if latest_release.created_at > release_date and latest_release.tag_name != metadata_version:
      print(f'*WARNING* {dep} has a newer release than {metadata_version}@<{release_date}>: '
            f'{latest_release.tag_name}@<{latest_release.created_at}>')
  except github.UnknownObjectException:
    pass


# Print GitHub release date, throw ReleaseDateError on mismatch with metadata release date.
def VerifyAndPrintReleaseDate(dep, github_release_date, metadata_release_date):
  mismatch = ''
  iso_release_date = FormatUtcDate(github_release_date)
  print(f'{dep} has a GitHub release date {iso_release_date}')
  if iso_release_date != metadata_release_date:
    raise ReleaseDateError(f'Mismatch with metadata release date of {metadata_release_date}')


# Extract release date from GitHub API.
def GetReleaseDate(repo, metadata_version, github_release):
  if github_release.tagged:
    tags = repo.get_tags()
    for tag in tags:
      if tag.name == github_release.version:
        return tag.commit.commit.committer.date
    return None
  else:
    assert (metadata_version == github_release.version)
    commit = repo.get_commit(github_release.version)
    return commit.commit.committer.date


# Verify release dates in metadata against GitHub API.
def VerifyAndPrintReleaseDates(repository_locations, github_instance):
  for dep, metadata in sorted(repository_locations.items()):
    release_date = None
    # Obtain release information from GitHub API.
    github_release = utils.GetGitHubReleaseFromUrls(metadata['urls'])
    if not github_release:
      print(f'{dep} is not a GitHub repository')
      continue
    repo = github_instance.get_repo(f'{github_release.organization}/{github_release.project}')
    release_date = GetReleaseDate(repo, metadata['version'], github_release)
    if release_date:
      # Check whether there is a more recent version and warn if necessary.
      VerifyAndPrintLatestRelease(dep, repo, github_release.version, release_date)
      # Verify that the release date in metadata and GitHub correspond,
      # otherwise throw ReleaseDateError.
      VerifyAndPrintReleaseDate(dep, release_date, metadata['release_date'])
    else:
      raise ReleaseDateError(f'{dep} is a GitHub repository with no no inferrable release date')


if __name__ == '__main__':
  if len(sys.argv) != 2:
    print('Usage: %s <path to repository_locations.bzl>' % sys.argv[0])
    sys.exit(1)
  access_token = os.getenv('GITHUB_TOKEN')
  if not access_token:
    print('Missing GITHUB_TOKEN')
    sys.exit(1)
  path = sys.argv[1]
  spec_loader = utils.repository_locations_utils.load_repository_locations_spec
  path_module = utils.LoadModule('repository_locations', path)
  try:
    VerifyAndPrintReleaseDates(spec_loader(path_module.REPOSITORY_LOCATIONS_SPEC),
                               github.Github(access_token))
  except ReleaseDateError as e:
    print(f'An error occurred while processing {path}, please verify the correctness of the '
          f'metadata: {e}')
