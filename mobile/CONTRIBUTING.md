We welcome contributions from the community. Please read the following guidelines carefully to
maximize the chances of your PR being merged.

# Communication

* Before starting work on a major feature, please reach out to us via GitHub, Slack,
  email, etc. We will make sure no one else is already working on it and ask you to open a
  GitHub issue.
* A "major feature" is defined as any change that is > 100 LOC altered (not including tests), or
  changes any user-facing behavior. We will use the GitHub issue to discuss the feature and come to
  agreement. This is to prevent your time being wasted, as well as ours. The GitHub review process
  for major features is also important so that [organizations with commit access](OWNERS.md) can
  come to agreement on design. If it is appropriate to write a design document, the document must
  be hosted either in the GitHub tracking issue, or linked to from the issue and hosted in a
  world-readable location.
* Small patches and bug fixes don't need prior communication.

# Coding style

* See [STYLE.md](STYLE.md)

# Submitting a PR

* Fork the repo.
* In your local repo, install the git hooks that implement various important pre-commit and
  pre-push checks:

  ```bash
  ./envoy/support/bootstrap
  ```

  Please see [envoy/support/README.md](https://github.com/envoyproxy/envoy/tree/master/support) for
  more information on these hooks.

* Create your PR.
* Tests will automatically run for you.
* We will **not** merge any PR that is not passing tests.
* PRs are expected to have 100% test coverage for added code. If your PR cannot have 100% coverage
  for some reason please clearly explain why when you open it.
* Any PR that changes user-facing behavior **must** have associated documentation in [docs](./docs)
  as well as [release notes](./docs/root/intro/version_history.rst).
* All code comments and documentation are expected to have proper English grammar and punctuation.
  If you are not a fluent English speaker (or a bad writer ;-)) please let us know and we will try
  to find some help.
* Your PR title should be descriptive, and generally start with a subsystem name followed by a
  colon. Examples:
  * "docs: fix grammar error"
  * "http conn man: add new feature"
* Your PR description should have details on what the PR does. If it fixes an existing issue it
  should end with "Fixes #XXX", so that the issue is closed when your PR merges.
* When all of the tests are passing and all other conditions described herein are satisfied, a
  maintainer will be assigned to review and merge the PR.
* Once you submit a PR, *please do not rebase it*. It's much easier to review if subsequent commits
  are new commits and/or merges. We squash rebase the final merged commit so the number of commits
  you have in the PR doesn't matter.
* We expect that once a PR is opened, it will be actively worked on until it is merged or closed.
  Stalebot will close PRs that are not making progress. This is defined as no activity
  for 14 days. Obviously PRs that are closed due to lack of activity can be reopened later.
  Closing stale PRs helps us to keep on top of all of the work currently in flight.
* Please consider joining the [envoy-mobile-dev](https://groups.google.com/forum/#!forum/envoy-mobile-dev)
  mailing list.

# PR review policy for maintainers

* Typically we try to turn around reviews within one business day.
* See [OWNERS.md](OWNERS.md) for the current list of maintainers.
* It is generally expected that a "domain expert" for the code the PR touches should review the
  PR. This person does not necessarily need to be a maintainer.
* The above rule may be waived for PRs which only update docs or comments, or trivial changes to
  tests and tools (where trivial is decided by the maintainer in question).
* If there is a question on who should review a PR please discuss in the #envoy-mobile room in
  Envoy Slack.
* Anyone is welcome to review any PR that they want, whether they are a maintainer or not.
* Please **clean up the title and body** before merging. By default, GitHub fills the squash merge
  title with the original title, and the commit body with every individual commit from the PR.
  The maintainer doing the merge should make sure the title follows the guidelines above and should
  overwrite the body with the original extended description from the PR (cleaning it up if necessary)
  while preserving the PR author's final DCO sign-off.

# DCO: Sign your work

Envoy Mobile ships commit hooks that allow you to auto-generate the DCO signoff line if
it doesn't exist when you run `git commit`. Simply navigate to the project
root and run:

```bash
./envoy/support/bootstrap
```

From here, simply commit as normal, and you will see the signoff at the bottom
of each commit.

The sign-off is a simple line at the end of the explanation for the
patch, which certifies that you wrote it or otherwise have the right to
pass it on as an open-source patch. The rules are pretty simple: if you
can certify the below (from
[developercertificate.org](https://developercertificate.org/)):

```
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.
660 York Street, Suite 102,
San Francisco, CA 94110 USA

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.


Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```

then you just add a line to every git commit message:

    Signed-off-by: Joe Smith <joe@gmail.com>

using your real name (sorry, no pseudonyms or anonymous contributions.)

You can add the sign off when creating the git commit via `git commit -s`.

If you want this to be automatic you can set up some aliases:

```bash
git config --add alias.amend "commit -s --amend"
git config --add alias.c "commit -s"
```

## Fixing DCO

If your PR fails the DCO check, it's necessary to fix the entire commit history in the PR. Best
practice is to [squash](https://gitready.com/advanced/2009/02/10/squashing-commits-with-rebase.html)
the commit history to a single commit, append the DCO sign-off as described above, and [force
push](https://git-scm.com/docs/git-push#git-push---force). For example, if you have 2 commits in
your history:

```bash
git rebase -i HEAD^^
(interactive squash + DCO append)
git push origin -f
```

Note, that in general rewriting history in this way is a hindrance to the review process and this
should only be done to correct a DCO mistake.

## Triggering CI re-run without making changes

Sometimes tasks may get stuck in CI and won't be marked as failed, which means
the above command won't work. Should this happen, pushing an empty commit should
re-run all the CI tasks. Consider adding an alias into your `.gitconfig` file:

```
[alias]
    kick-ci = !"git commit -s --allow-empty -m 'Kick CI' && git push"
```

Once you add this alias you can issue the command `git kick-ci` and the PR
will be sent back for a retest.
