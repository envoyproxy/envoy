
def pr_has_commits_without_dco(context, state):
  return True

def send_dco_message(sender):
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

def _status(sender, context,  state):
  if pr_has_commits_without_dco(context, state):
    send_dco_message(sender)

handlers.status(func=_status)
