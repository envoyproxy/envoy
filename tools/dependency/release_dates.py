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

import exports
import utils

from colorama import Fore, Style
from packaging import version


# Thrown on errors related to release date or version.
class ReleaseDateVersionError(Exception):
    pass


# Format a datetime object as UTC YYYY-MM-DD.
def format_utc_date(date):
    # We only handle naive datetime objects right now, which is what PyGithub
    # appears to be handing us.
    if date.tzinfo is not None:
        raise ReleaseDateVersionError(
            "Expected UTC date without timezone information. Received timezone information")
    return date.date().isoformat()


# Obtain latest release version and compare against metadata version, warn on
# mismatch.
def verify_and_print_latest_release(dep, repo, metadata_version, release_date):
    try:
        latest_release = repo.get_latest_release()
    except github.GithubException as err:
        # Repositories can not have releases or if they have releases may not publish a latest releases. Return
        print(f'GithubException {repo.name}: {err.data} {err.status} while getting latest release.')
        return
    if latest_release.created_at > release_date and latest_release.tag_name != metadata_version:
        print(
            f'{Fore.YELLOW}*WARNING* {dep} has a newer release than {metadata_version}@<{release_date}>: '
            f'{latest_release.tag_name}@<{latest_release.created_at}>{Style.RESET_ALL}')


# Print GitHub release date, throw ReleaseDateVersionError on mismatch with metadata release date.
def verify_and_print_release_date(dep, github_release_date, metadata_release_date):
    mismatch = ''
    iso_release_date = format_utc_date(github_release_date)
    print(f'{Fore.GREEN}{dep} has a GitHub release date {iso_release_date}{Style.RESET_ALL}')
    if iso_release_date != metadata_release_date:
        raise ReleaseDateVersionError(
            f'Mismatch with metadata release date of {metadata_release_date}')


# Extract release date from GitHub API for tagged releases.
def get_tagged_release_date(repo, metadata_version, github_release):

    try:
        latest = repo.get_latest_release()
    except github.GithubException as err:
        # Repositories can not have releases or if they have releases may not publish a latest releases. If this is the case we keep going
        latest = ''
        print(f'GithubException {repo.name}: {err.data} {err.status} while getting latest release.')

    if latest and github_release.version <= latest.tag_name:
        release = repo.get_release(github_release.version)
        return release.published_at
    else:
        tags = repo.get_tags()
        current_metadata_tag_commit_date = ''
        for tag in tags.reversed:
            if tag.name == github_release.version:
                current_metadata_tag_commit_date = tag.commit.commit.committer.date
            if not version.parse(tag.name).is_prerelease and version.parse(
                    tag.name) > version.parse(github_release.version):
                print(
                    f'{Fore.YELLOW}*WARNING* {repo.name} has a newer release than {github_release.version}@<{current_metadata_tag_commit_date}>: '
                    f'{tag.name}@<{tag.commit.commit.committer.date}>{Style.RESET_ALL}')
        return current_metadata_tag_commit_date


# Extract release date from GitHub API for untagged releases.
def get_untagged_release_date(repo, metadata_version, github_release):
    if metadata_version != github_release.version:
        raise ReleaseDateVersionError(
            f'Mismatch with metadata version {metadata_version} and github release version {github_release.version}'
        )
    commit = repo.get_commit(github_release.version)
    commits = repo.get_commits(since=commit.commit.committer.date)
    if commits.totalCount > 1:
        print(
            f'{Fore.YELLOW}*WARNING* {repo.name} has {str(commits.totalCount - 1)} commits since {github_release.version}@<{commit.commit.committer.date}>{Style.RESET_ALL}'
        )
    return commit.commit.committer.date


# Verify release dates in metadata against GitHub API.
def verify_and_print_release_dates(repository_locations, github_instance):
    for dep, metadata in sorted(repository_locations.items()):
        release_date = None
        # Obtain release information from GitHub API.
        github_release = utils.get_github_release_from_urls(metadata['urls'])
        print('github_release: ', github_release)
        if not github_release:
            print(f'{dep} is not a GitHub repository')
            continue
        repo = github_instance.get_repo(f'{github_release.organization}/{github_release.project}')
        if github_release.tagged:
            release_date = get_tagged_release_date(repo, metadata['version'], github_release)
        else:
            release_date = get_untagged_release_date(repo, metadata['version'], github_release)
        if release_date:
            # Check whether there is a more recent version and warn if necessary.
            verify_and_print_latest_release(dep, repo, github_release.version, release_date)
            # Verify that the release date in metadata and GitHub correspond,
            # otherwise throw ReleaseDateVersionError.
            verify_and_print_release_date(dep, release_date, metadata['release_date'])
        else:
            raise ReleaseDateVersionError(
                f'{dep} is a GitHub repository with no no inferrable release date')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print('Usage: %s <path to repository_locations.bzl>' % sys.argv[0])
        sys.exit(1)
    access_token = os.getenv('GITHUB_TOKEN')
    if not access_token:
        print('Missing GITHUB_TOKEN')
        sys.exit(1)
    path = sys.argv[1]
    spec_loader = exports.repository_locations_utils.load_repository_locations_spec
    path_module = exports.load_module('repository_locations', path)
    try:
        verify_and_print_release_dates(
            spec_loader(path_module.REPOSITORY_LOCATIONS_SPEC), github.Github(access_token))
    except ReleaseDateVersionError as e:
        print(
            f'{Fore.RED}An error occurred while processing {path}, please verify the correctness of the '
            f'metadata: {e}{Style.RESET_ALL}')
        sys.exit(1)
