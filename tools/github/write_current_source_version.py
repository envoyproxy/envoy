# Copyright 2020 Envoyproxy Authors

# Produces SOURCE_VERSION file with content from current version commit hash. As a reminder,
# SOURCE_VERSION is required when building Envoy from an extracted release tarball (non-git).
# See: bazel/get_workspace_status for more information.
#
# The SOURCE_VERSION is produced by reading the VERSION.txt file then fetch the corresponding commit
# hass from GitHub. Note: This script can only be executed from project root directory of an
# extracted "release" tarball.

import os
import sys
import json

from urllib import request
from pathlib import Path

if __name__ == "__main__":
    # Simple check if a .git directory exists. We should only rely on information from "git".
    if Path(".git").exists():
        print(
            "Failed to create SOURCE_VERSION. "
            "Run this script from an extracted release tarball directory."
        )
        sys.exit(1)

    # Check if we have VERSION.txt available
    current_version_file = Path("VERSION.txt")
    if not current_version_file.exists():
        print(
            "Failed to read VERSION.txt. "
            "Run this script from project root of an extracted release tarball directory."
        )
        sys.exit(1)

    current_version = current_version_file.read_text().rstrip()

    # Exit when we are in a "main" copy.
    if current_version.endswith("-dev"):
        print(
            "Failed to create SOURCE_VERSION. "
            "The current VERSION.txt contains version with '-dev' suffix. "
            "Run this script from an extracted release tarball directory."
        )
        sys.exit(1)

    # Fetch the current version commit information from GitHub.
    with request.urlopen(
        "https://api.github.com/repos/envoyproxy/envoy/commits/v" + current_version
    ) as response:
        commit_info = json.loads(response.read())
        source_version_file = Path("SOURCE_VERSION")
        with source_version_file.open("w", encoding="utf-8") as source_version:
            # Write the extracted current version commit hash "sha" to SOURCE_VERSION.
            source_version.write(commit_info["sha"])
