
# not sure if this is needed, or whether `pull_request` below has info already
def get_pr_author_association(issue_number):
  return github.call(
    method="GET",
    path="repos/envoyproxy/envoy/pulls/%s" % issue_number)["json"]["author_association"]

def is_newcontributor(pull_request):
  # check for any existing open PRs from user ?
  return pull_request["author_association"] == "FIRST_TIME_CONTRIBUTOR"

def should_message_newcontributor(action, pull_request):
  return (
    action == 'opened'
    and is_newcontributor(pull_request))

def send_newcontributor_message(sender):
  github.issue_create_comment("""
hi @%s, welcome and thankyou for your contribution.

We will try to review your Pull Request as quickly as possible.

In the meantime, please take a look at the [contribution guidelines](https://github.com/envoyproxy/envoy/blob/master/CONTRIBUTING.md) if you have not done so already.

""" % sender)

def _pr(action, pull_request, sender, config):
  if should_message_newcontributor(action, pull_request):
    send_newcontributor_message(sender)

handlers.pull_request(func=_pr)
