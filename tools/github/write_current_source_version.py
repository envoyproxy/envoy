# This script produces SOURCE_VERSION file with content from current version commit hash. As a
# reminder,SOURCE_VERSION is required when building Envoy from an extracted release tarball
# (non-git). See: bazel/get_workspace_status for more information.
#
# The SOURCE_VERSION file is produced by reading current version tag from VERSION.txt file then
# fetch the corresponding commit hash from GitHub.
#
# Note: This script can only be executed from project root directory of an extracted "release"
# tarball.

import argparse
import json
import os
import pathlib
import sys
import urllib.request

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Write current source version")
    parser.add_argument(
        "--skip_error_in_git",
        dest="skip_error_in_git",
        help="Skip returning error on exit when the current directory is a git repository.",
        action="store_true")
    parser.add_argument(
        "--github_api_token_env_name",
        dest="github_api_token_env_name",
        help="The system environment variable name that holds GitHub API token. "
        "This is advisable to provide this to avoid rate-limited calls.",
        type=str,
        action="store",
        default="GITHUB_TOKEN")
    args = parser.parse_args()

    # Simple check if a .git directory exists. When we are in a Git repo, we should rely on git.
    if pathlib.Path(".git").exists():
        print(
            "Failed to create SOURCE_VERSION. "
            "Run this script from an extracted release tarball directory.")
        if args.skip_error_in_git:
            # We can optionally "silent" the error and the workspace status check will be done using
            # git instead.
            print("Workspace status check will be done using git.")
            sys.exit(0)
        sys.exit(1)

    # Get the project root directory (../../..).
    project_root_dir = pathlib.PurePath(__file__).parent.parent.parent

    # Check if we have VERSION.txt available
    current_version_file = pathlib.Path(project_root_dir, "VERSION.txt")
    if not current_version_file.exists():
        print(
            "Failed to read VERSION.txt. "
            "Run this script from project root of an extracted release tarball directory.")
        sys.exit(1)

    current_version = current_version_file.read_text().rstrip()

    # Exit when we are in a "main" copy.
    if current_version.endswith("-dev"):
        print(
            "Failed to create SOURCE_VERSION. "
            "The current VERSION.txt contains version with '-dev' suffix. "
            "Run this script from an extracted release tarball directory.")
        sys.exit(1)

    # Fetch the current version commit information from GitHub.
    commit_info_request = urllib.request.Request(
        "https://api.github.com/repos/envoyproxy/envoy/commits/v" + current_version)
    github_token = os.environ.get(args.github_api_token_env_name)
    if github_token != None:
        # To avoid rate-limited API calls.
        commit_info_request.add_header("Authorization", "Bearer " + github_token)
    response = urllib.request.urlopen(commit_info_request)
    commit_info = json.loads(response.read())
    source_version_file = pathlib.Path(project_root_dir, "SOURCE_VERSION")
    # Write the extracted current version commit hash "sha" to SOURCE_VERSION.
    source_version_file.write_text(commit_info["sha"])
