# Flags when a Pull request makes changes to VERSION.txt
#
# use("github.com/repokitteh/modules/versionchange.star")
#

load("text", "match")

# TODO(phlax): put this in config
VERSION_CHANGE_LABEL = "version-change"


def _check_version_changes():
    matched = [f for f in github.pr_list_files() if match("VERSION.txt", f['filename'])]
    if matched:
        github.issue_label(VERSION_CHANGE_LABEL)


def _pr(action):
    if action in ['synchronize', 'opened']:
        _check_version_changes()


handlers.pull_request(func=_pr)
