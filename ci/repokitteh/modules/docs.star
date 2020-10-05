

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

def _docs_build_url(config, repo_owner):
  status = [
    _status
    for _status
    in github.get_combined_statuses()["statuses"]
    if _status["context"] == "ci/circleci: docs"]
  status = (
    status[0]
    if status
    else None)
  if not status:
    github.issue_create_comment("couldnt find status...")
    return
  m = match(text=status["target_url"], pattern='/([0-9]+)\?')
  build_id = (
    int(m[1])
    if m and len(m) == 2
    else None)
  if not build_id:
    github.issue_create_comment("couldnt find build id...")
    return
  artifacts = circleci_call(
    repo_owner,
    'envoy',
    build_id,
    'artifacts',
    config["token"],
    filter="successful")['json']
  index = [
    arti
    for arti
    in artifacts or []
    if arti["path"] == "generated/docs/index.html"]
  if not index:
    github.issue_create_comment("couldnt find generated index page...")
    return
  github.issue_create_comment(index[0]["url"])

handlers.status(func=_status)
handlers.command(name='docs', func=_docs_build_url)
