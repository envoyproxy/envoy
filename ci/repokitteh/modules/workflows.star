# Flags when a Pull request makes changes to .github/
#
# use("github.com/repokitteh/modules/workflows.star")
#

load("text", "match")

UNTESTED_WORKFLOWS_LABEL = "workflows:untested"


def _check_workflow_changes():
    matched = [f for f in github.pr_list_files() if match("^\.github/.*", f['filename'])]
    if matched:
        github.issue_label(UNTESTED_WORKFLOWS_LABEL)


def _pr(action):
    if action in ['synchronize', 'opened']:
        _check_workflow_changes()


handlers.pull_request(func=_pr)
