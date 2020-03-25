#!/bin/bash

# Looking back from HEAD, find the first commit that was merged onto master by GitHub. This is
# likely the last non-local change on a given branch. There may be some exceptions for this
# heuristic, e.g. when patches are manually merged for security fixes on master, but this is very
# rare.

git rev-list --no-merges --committer="GitHub <noreply@github.com>" --max-count=1 HEAD
