#!/bin/bash
# Script to add sign-off only to commits by Shane Yuan

# Get the commit message from the file
commit_msg=$(cat "$1")

# Check if this commit is by Shane Yuan
if [ "$GIT_AUTHOR_EMAIL" = "hyuan@salesforce.com" ]; then
    # Check if sign-off already exists
    if echo "$commit_msg" | grep -q "Signed-off-by:"; then
        # Sign-off already exists, just pass through
        cat "$1"
    else
        # Add sign-off
        echo "$commit_msg"
        echo ""
        echo "Signed-off-by: Shane Yuan <hyuan@salesforce.com>"
    fi
else
    # Not Shane Yuan's commit, pass through unchanged
    cat "$1"
fi
