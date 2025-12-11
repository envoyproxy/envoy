COVERAGE_LINK_MESSAGE = """

Coverage for this Pull Request will be rendered here:

https://storage.googleapis.com/envoy-cncf-pr/%s/coverage/index.html

For comparison, current coverage on `main` branch is here:

https://storage.googleapis.com/envoy-cncf-postsubmit/main/coverage/index.html

The coverage results are (re-)rendered each time the CI `Envoy/Checks (coverage)` job completes.

"""

def should_add_coverage_link(action, issue_title):
    return (
        action == "opened" and
        issue_title.startswith("coverage:")
    )

def add_coverage_link(issue_number):
    github.issue_create_comment(COVERAGE_LINK_MESSAGE % issue_number)

def _pr(action, issue_number, issue_title):
    if should_add_coverage_link(action, issue_title):
        add_coverage_link(issue_number)

def _add_coverage(issue_number):
    add_coverage_link(issue_number)

handlers.pull_request(func = _pr)
handlers.command(name = "coverage", func = _add_coverage)
