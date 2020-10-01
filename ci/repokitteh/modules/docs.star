

def docs_have_been_rebuilt(context, state):
  return (
    context == "circleci: docs"
    and state == "success")

def docs_have_changed_in_this_pr():
  return bool([
    f["filename"]
    for f
    in github.pr_list_files()
    if (f["filename"].startswith("docs/")
        or f["filename"].startswith("api/"))])

def docs_have_changed_between_commits(commit1, commit2):
  author = "phlax"
  return bool([
    f["filename"]
    for f
    in github.call("/repos/%s/envoy/compare/%s...%s" % (author, commit1, commit2))
    if (f["filename"].startswith("docs/")
        or f["filename"].startswith("api/"))])

def docs_have_changed_since_last_rebuild(pull_request, commit):
  if not docs_have_changed_in_this_pr():
    return False

  # check if there is a stored commit hash for last rendered docs on this PR

  # check if there are changes between that commit hash and current HEAD

  # store this commit has if there are changes and return True
  return True

def get_rendered_docs_url():
  return "https://new.docs.url/changed/docs"

def send_docs_message(pull_request, commit):
  # query circleci api to get URL of rendered docs
  github.issue_create_comment("""
Documentation has changed. You can view the rendered changes here:

%s
""" % get_rendered_docs_url(pull_request, commit)


def _status(context, state):
  should_send_message = (
    docs_have_been_rebuilt(context, state)
    and docs_have_changed_since_last_rebuild(pull_request, commit))
  if should_send_message:
    send_docs_message(pull_request, commit)

handlers.status(func=_status)
