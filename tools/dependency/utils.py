# Utilities for reasoning about dependencies.

from collections import namedtuple
from importlib.util import spec_from_loader, module_from_spec
from importlib.machinery import SourceFileLoader


# Shared Starlark/Python files must have a .bzl suffix for Starlark import, so
# we are forced to do this workaround.
def LoadModule(name, path):
  spec = spec_from_loader(name, SourceFileLoader(name, path))
  module = module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


envoy_repository_locations = LoadModule('envoy_repository_locations',
                                        'bazel/repository_locations.bzl')
api_repository_locations = LoadModule('api_repository_locations',
                                      'api/bazel/repository_locations.bzl')
repository_locations_utils = LoadModule('repository_locations_utils',
                                        'api/bazel/repository_locations_utils.bzl')


# All repository location metadata in the Envoy repository.
def RepositoryLocations():
  spec_loader = repository_locations_utils.load_repository_locations_spec
  locations = spec_loader(envoy_repository_locations.REPOSITORY_LOCATIONS_SPEC)
  locations.update(spec_loader(api_repository_locations.REPOSITORY_LOCATIONS_SPEC))
  return locations


# Information releated to a GitHub release version.
GitHubRelease = namedtuple('GitHubRelease', ['organization', 'project', 'version', 'tagged'])


# Search through a list of URLs and determine if any contain a GitHub URL. If
# so, use heuristics to extract the release version and repo details, return
# this, otherwise return None.
def GetGitHubReleaseFromUrls(urls):
  for url in urls:
    if not url.startswith('https://github.com/'):
      continue
    components = url.split('/')
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
    # If it's not a GH hash, it's a tagged release.
    tagged_release = len(github_version) != 40
    return GitHubRelease(organization=components[3],
                         project=components[4],
                         version=github_version,
                         tagged=tagged_release)
  return None
