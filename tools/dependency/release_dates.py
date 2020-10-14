# CLI tool to query GitHub API and compare with repository_locations.bzl. It
# will:
# - Compute the release date of dependencies and warn if there is a mismatch
#   with the metdata release date.
# - Look at the latest release tag and warn if this is later than the dependency
#   version in the .bzl.
#
# Usage:
#   tools/dependency/release_dates.sh <path to repository_locations.bzl>
#
# You will need to set a GitHub access token in the GH_ACCESS_TOKEN environment
# variable. You can general personal access tokens under developer settings on
# GitHub. You should restrict the scope of the token to "repo: public_repo".

from importlib.util import spec_from_loader, module_from_spec
from importlib.machinery import SourceFileLoader
import os
import sys

import github


# TODO(htuch): refactor with docs/generate_external_dep_rst.py and validate.py.
# Shared Starlark/Python files must have a .bzl suffix for Starlark import, so
# we are forced to do this workaround.
def LoadModule(name, path):
  spec = spec_from_loader(name, SourceFileLoader(name, path))
  module = module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


repository_locations_utils = LoadModule('repository_locations_utils',
                                        'api/bazel/repository_locations_utils.bzl')


# TODO(htuch): refactor with docs/generate_external_dep_rst.py
def GetVersionFromGitHubUrl(github_url):
  components = github_url.split('/')
  if components[5] == 'archive':
    # Only support .tar.gz, .zip today. Figure out the release tag from this
    # filename.
    if components[6].endswith('.tar.gz'):
      github_version = components[6][:-len('.tar.gz')]
    else:
      assert (components[6].endswith('.zip'))
      github_version = components[6][:-len('.zip')]
  else:
    # Release tag is a path component.
    assert (components[5] == 'releases')
    github_version = components[7]
  return github_version


def FormatUtcDate(date):
  # We only handle naive datetime objects right now, which is what PyGithub
  # appears to be handing us.
  assert (date.tzinfo is None)
  return date.date().isoformat()


def PrintReleaseDates(repository_locations, github_instance):
  # TODO(htuch): This would benefit from some more decomposition and
  # restructuring.
  for dep, metadata in sorted(repository_locations.items()):
    release_date = None
    warning = None
    is_github_repository = False
    for url in metadata['urls']:
      if url.startswith('https://github.com/'):
        is_github_repository = True
        organization, project = url.split('/')[3:5]
        repo = github_instance.get_repo(f'{organization}/{project}')
        github_version = GetVersionFromGitHubUrl(url)
        metadata_version = metadata['version']
        tagged_release = len(metadata_version) != 40
        if tagged_release:
          tags = repo.get_tags()
          for tag in tags:
            if tag.name == github_version:
              release_date = tag.commit.commit.committer.date
          if not release_date:
            warning = 'No matching tag'
        else:
          assert (metadata_version == github_version)
          commit = repo.get_commit(github_version)
          release_date = commit.commit.committer.date
    if warning:
      print(f'*WARNING* {dep}: {warning}')
    if release_date:
      try:
        latest_release = repo.get_latest_release()
        if latest_release.created_at > release_date and latest_release.tag_name != github_version:
          print(
              f'*WARNING* {dep} has a newer release than {github_version}@<{release_date}>: {latest_release.tag_name}@<{latest_release.created_at}>'
          )
      except github.UnknownObjectException:
        pass
      mismatch = ''
      iso_release_date = FormatUtcDate(release_date)
      if iso_release_date != metadata['release_date']:
        mismatch = ' [MISMATCH]'
      print(f'{dep} has a release date {iso_release_date}{mismatch}')
    elif not is_github_repository:
      print(f'{dep} is not a GitHub repository')
    else:
      print(f'{dep} is a GitHub repository with no no inferrable release date')


if __name__ == '__main__':
  if len(sys.argv) != 2:
    print('Usage: %s <path to repository_locations.bzl>' % sys.argv[0])
    sys.exit(1)
  access_token = os.getenv('GH_ACCESS_TOKEN')
  if not access_token:
    print('Missing GH_ACCESS_TOKEN')
    sys.exit(1)
  path = sys.argv[1]
  spec_loader = repository_locations_utils.load_repository_locations_spec
  path_module = LoadModule('repository_locations', path)
  PrintReleaseDates(spec_loader(path_module.REPOSITORY_LOCATIONS_SPEC), github.Github(access_token))
