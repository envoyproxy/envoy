
NEW_CONTRIBUTOR_MESSAGE = """
Hi @%s, welcome and thank you for your contribution.

We will try to review your Pull Request as quickly as possible.

In the meantime, please take a look at the [contribution guidelines](https://github.com/envoyproxy/envoy/blob/master/CONTRIBUTING.md) if you have not done so already.

"""

def get_pr_author_association(issue_number):
  return github.call(
    method="GET",
    path="repos/envoyproxy/envoy/pulls/%s" % issue_number)["json"]["author_association"]

def is_newcontributor(issue_number):
  return get_pr_author_association(issue_number) == "FIRST_TIME_CONTRIBUTOR"

def should_message_newcontributor(action, issue_number):
  return (
    action == 'opened'
    and is_newcontributor(issue_number))

def send_newcontributor_message(sender):
  github.issue_create_comment(NEW_CONTRIBUTOR_MESSAGE % sender)

def _pr(action, issue_number, sender, config):
  if should_message_newcontributor(action, issue_number):
    send_newcontributor_message(sender)

handlers.pull_request(func=_pr)
