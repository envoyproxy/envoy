
DOCS_LINK_MESSAGE = """

Docs for this Pull Request will be rendered here:

https://storage.googleapis.com/envoy-pr/%s/docs/index.html

The docs are (re-)rendered each time the CI `envoy-presubmit (precheck docs)` job completes.

"""

def should_add_docs_link(action, issue_title):
    return (
        action == 'opened'
        and issue_title.startswith("docs:"))

def add_docs_link(issue_number):
    github.issue_create_comment(DOCS_LINK_MESSAGE % issue_number)

def _pr(action, issue_number, issue_title):
    if should_add_docs_link(action, issue_title):
        add_docs_link(issue_number)

def _add_docs(issue_number):
    add_docs_link(issue_number)

handlers.pull_request(func=_pr)
handlers.command(name='docs', func=_add_docs)
