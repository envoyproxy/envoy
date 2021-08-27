# Utilities for reasoning about dependencies.

from collections import namedtuple

from tools.dependency.exports import (
    api_repository_locations, envoy_repository_locations, repository_locations_utils)


# All repository location metadata in the Envoy repository.
def repository_locations():
    spec_loader = repository_locations_utils.load_repository_locations_spec
    locations = spec_loader(envoy_repository_locations.REPOSITORY_LOCATIONS_SPEC)
    locations.update(spec_loader(api_repository_locations.REPOSITORY_LOCATIONS_SPEC))
    return locations


# Obtain GitHub project URL from a list of URLs.
def get_github_project_url(urls):
    for url in urls:
        if not url.startswith('https://github.com/'):
            continue
        components = url.split('/')
        return f'https://github.com/{components[3]}/{components[4]}'
    return None


# Information releated to a GitHub release version.
GitHubRelease = namedtuple('GitHubRelease', ['organization', 'project', 'version', 'tagged'])


# Search through a list of URLs and determine if any contain a GitHub URL. If
# so, use heuristics to extract the release version and repo details, return
# this, otherwise return None.
def get_github_release_from_urls(urls):
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
        return GitHubRelease(
            organization=components[3],
            project=components[4],
            version=github_version,
            tagged=tagged_release)
    return None
