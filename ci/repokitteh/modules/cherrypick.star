HELP_MESSAGE = """**Usage:** `/cherrypick <branch> [<branch> ...]`

Cherry-picks the merged commit(s) from this pull request into the specified release \
branch(es) and creates new pull request(s).

**Requirements:**
- The PR must be merged before running this command.
- Specify between 1 and 4 target branches.

**Example:**
```
/cherrypick release/v1.37 release/v1.38
```
"""

def _cherrypick(issue_number, command, sender):
    branches = command.args

    if len(branches) == 0 or len(branches) > 4 or branches[0] in ["help", "?"]:
        github.issue_create_comment(HELP_MESSAGE)
        return

    pr = github.call(
        method = "GET",
        path = "repos/envoyproxy/envoy/pulls/%s" % issue_number,
    )["json"]

    pr_author = pr["user"]["login"]
    if sender != pr_author and not github.repo_is_collaborator(sender):
        github.issue_create_comment(
            "@%s only the PR author or a repo maintainer can use the `/cherrypick` command." % sender,
        )
        return

    if not pr["merged"]:
        github.issue_create_comment(
            "This PR has not been merged yet. " +
            "The `/cherrypick` command can only be used after the PR is merged.\n\n" +
            HELP_MESSAGE,
        )
        return

    # success_codes includes 404/422 so that github.call() returns the response
    # instead of raising, allowing us to handle errors gracefully.
    resp = github.call(
        method = "POST",
        path = "repos/envoyproxy/envoy/dispatches",
        body = {
            "event_type": "cherrypick",
            "client_payload": {
                "pr_number": issue_number,
                "target_branches": " ".join(branches),
                "merge_commit_sha": pr["merge_commit_sha"],
            },
        },
        success_codes = [204, 404, 422],
    )

    if resp["status"] == 204:
        branch_list = ", ".join(["`%s`" % b for b in branches])
        github.issue_create_comment(
            "Cherry-pick initiated to branches: %s. " % branch_list +
            "Results will be posted here once completed.",
        )
    else:
        github.issue_create_comment(
            "Failed to trigger the cherry-pick workflow. " +
            "Please check the workflow configuration.",
        )

handlers.command(name = "cherrypick", func = _cherrypick)
handlers.command(name = "cherry-pick", func = _cherrypick)
