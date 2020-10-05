load('text', 'match')
load("json", "from_json")

def _woof0(sender):
  github.issue_create_comment(
    dict(response=github.call(
      method="GET",
      path="repos/envoyproxy/envoy/statuses/27d831a949b104db79f17707ca679a6af26e40fe")))


def _woof1(sender):
  github.issue_create_comment(github.issue_list_comments()[0])


def get_pr_author_association(issue_number):
  return github.call(
    method="GET",
    path="repos/envoyproxy/envoy/pulls/%s" % issue_number)["json"]["author_association"]

def get_pr_base_commit_sha(issue_number):
  return github.call(
    method="GET",
    path="repos/envoyproxy/envoy/pulls/%s" % issue_number)["json"]["base"]["sha"]


def woof_dco(sender, issue_number):
  github.issue_create_comment("""
hi @%s

  It seems that one or more of the commits in your Pull Request has not been signed with [DCO](https://github.com/envoyproxy/envoy/blob/master/DCO).

  We require this to ensure we know and remember who made each contribution.

  You can ensure your future commits are signed by running the following in the root of the Envoy repository:
  ```
  ./support/bootstrap
  git config --add alias.amend \"commit -s --amend\"
  git config --add alias.c \"commit -s\"
  ```

  You will still need to amend the problem commits in this PR to ensure the commit messages contain a line like:
  ```
  Signed-off-by: Joe Smith <joe@gmail.com>
  ```

  You can do this by rebasing and then squashing the commits and/or rewording the problem commit messages to include the sign-off.

  To rebase the last `N` commits:

  ```bash
  git rebase -i HEAD~N
  # (interactive reword/squash + DCO append)
  git push origin -f
  ```

  Full information can be found in the [contribution guidelines](https://github.com/envoyproxy/envoy/blob/master/CONTRIBUTING.md#dco-sign-your-work)""" % sender)


def woof_welcome(sender, issue_number):
  github.issue_create_comment("""
hi @%s, welcome and thankyou for your contribution.

We will try to review your Pull Request as quickly as possible.

In the meantime, please take a look at the [contribution guidelines](https://github.com/envoyproxy/envoy/blob/master/CONTRIBUTING.md) if you have not done so already.

""" % sender)

def _status():
  github.issue_create_comment("GOT STATUS EVENT!")

def docs_have_changed_in_this_pr():
  return bool([
    f["filename"]
    for f
    in github.pr_list_files()
    if (f["filename"].startswith("docs/")
        or f["filename"].startswith("api/"))])

def docs_have_changed_between_commits(author, commit1, commit2):
  return bool([
    f["filename"]
    for f
    in github.call(
      method="GET",
      path="/repos/%s/envoy/compare/%s...%s" % (author, commit1, commit2))
    if (f["filename"].startswith("docs/")
        or f["filename"].startswith("api/"))])

def woof_docs_have_changed_in_this_pr():
  github.issue_create_comment("Docs changed?: %s" % docs_have_changed_in_this_pr())

def woof_author_and_commits(issue_user, sha, issue_number):
  # github.issue_create_comment("Author: %s" % issue_user)
  # github.issue_create_comment("SHA: %s" % sha)
  # github.issue_create_comment("PR issue: %s" % issue_number)
  # github.issue_create_comment("Base SHA: %s" % get_pr_base_commit_sha(issue_number))
  base_sha = get_pr_base_commit_sha(issue_number)
  github.issue_create_comment(
    "Docs have changed between commits (%s...%s): %s"
    % (base_sha, sha, docs_have_changed_between_commits(issue_user, base_sha, sha)))

def woof_cleanup(config, repo_owner):
  _comments = github.issue_list_comments()
  comments = reversed([c for c in _comments if c['user']['login'] == 'phlax'])[:20]
  for comment in comments:
    github.call(method="DELETE", success_codes=[204], path="/".join(comment["url"].split("/")[3:]))
  github.issue_create_comment("done!")

def circleci_call(owner, repo, build_id, verb, token, method='GET', **kwargs):
  secret_url='https://circleci.com/api/v1.1/project/github/%s/%s/%d/%s?circle-token=%s' % (
    owner,
    repo,
    build_id,
    verb,
    token)
    # "&".join(["%s=%s" % (k, v) for k, v in kwargs.items()]))
  return http(
    method=method,
    headers={"Accept": "application/json"},
    secret_url=secret_url)

def woof_circle_artifacts(config, repo_owner):
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
    filter="successful")['body']
  github.issue_create_comment(artifacts)
  return
  index = [
    arti
    for arti
    in artifacts or []
    if arti["path"] == "generated/docs/index.html"]
  if not index:
    github.issue_create_comment("couldnt find generated index page...")
    return
  github.issue_create_comment(index["url"])


handlers.command(name='woof', func=woof_circle_artifacts)
# handlers.command(name='woof', func=woof_docs_have_changed_in_this_pr)
handlers.status(func=_status)
