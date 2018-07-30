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
  come to agreement on design.
* Specifically, if the goal is to add a new [extension](REPO_LAYOUT.md#sourceextensions-layout),
  please read the [extension policy](GOVERNANCE.md#extension-addition-policy).
* Small patches and bug fixes don't need prior communication.

# Coding style

* See [STYLE.md](STYLE.md)

# Breaking change policy

* As of the 1.3.0 release, the Envoy user-facing configuration is locked and we will not make
  breaking changes between official numbered releases. This includes JSON configuration, REST/gRPC
  APIs (SDS, CDS, RDS, etc.), and CLI switches. We will also try to not change behavioral semantics
  (e.g., HTTP header processing order), though this is harder to outright guarantee.
* We reserve the right to deprecate configuration, and at the beginning of the following release
  cycle remove the deprecated configuration. For example, all deprecations between 1.3.0 and
  1.4.0 will be deleted soon AFTER 1.4.0 is tagged and released (at the beginning of the 1.5.0
  release cycle).
* This policy means that organizations deploying master should have some time to get ready for
  breaking changes, but we make no guarantees about the length of time.
* The breaking change policy also applies to source level extensions (e.g., filters). Code that
  conforms to the public interface documentation should continue to compile and work within the
  deprecation window. Within this window, a warning of deprecation should be carefully logged (some
  features might need rate limiting for logging this). We make no guarantees about code or deployments
  that rely on undocumented behavior.
* All deprecations/breaking changes will be clearly listed in [DEPRECATED.md](DEPRECATED.md).
* All deprecations/breaking changes will be announced to the
  [envoy-announce](https://groups.google.com/forum/#!forum/envoy-announce) email list.

# Release cadence

* Currently we are targeting approximately quarterly official releases. We may change this based
  on customer demand.
* In general, master is assumed to be release candidate quality at all times for documented
  features. For undocumented or clearly under development features, use caution or ask about
  current status when running master. Lyft runs master in production, typically deploying every
  few days.
* Note that we currently do not provide binary packages (RPM, etc.). Organizations are expected to
  build Envoy from source. This may change in the future if we get resources for maintaining
  packages.

# Submitting a PR

* Fork the repo.
* In your local repo, install the git hooks that implement various important pre-commit and
  pre-push checks:

  ```
  ./support/bootstrap
  ```

  Please see [support/README.md](support/README.md) for more information on these hooks.

* Create your PR.
* Tests will automatically run for you.
* We will **not** merge any PR that is not passing tests.
* PRs are expected to have 100% test coverage for added code. This can be verified with a coverage
  build. If your PR cannot have 100% coverage for some reason please clearly explain why when you
  open it.
* Any PR that changes user-facing behavior **must** have associated documentation in [docs](docs) as
  well as [release notes](docs/root/intro/version_history.rst). API changes should be documented
  inline with protos as per the [API contribution guidelines](api/CONTRIBUTING.md).
* All code comments and documentation are expected to have proper English grammar and punctuation.
  If you are not a fluent English speaker (or a bad writer ;-)) please let us know and we will try
  to find some help but there are no guarantees.
* Your PR title should be descriptive, and generally start with a subsystem name followed by a
  colon. Examples:
  * "docs: fix grammar error"
  * "http conn man: add new feature"
* Your PR description should have details on what the PR does. If it fixes an existing issue it
  should end with "Fixes #XXX".
* When all of the tests are passing and all other conditions described herein are satisfied, tag
  @lyft/network-team and we will review it and merge.
* Once you submit a PR, *please do not rebase it*. It's much easier to review if subsequent commits
  are new commits and/or merges. We squash rebase the final merged commit so the number of commits
  you have in the PR don't matter.
* We expect that once a PR is opened, it will be actively worked on until it is merged or closed.
  We reserve the right to close PRs that are not making progress. This is generally defined as no
  changes for 7 days. Obviously PRs that are closed due to lack of activity can be reopened later.
  Closing stale PRs helps us to keep on top of all of the work currently in flight.
* If a commit deprecates a feature, the commit message must mention what has been deprecated.
  Additionally, [DEPRECATED.md](DEPRECATED.md) must be updated as part of the commit.
* Please consider joining the [envoy-dev](https://groups.google.com/forum/#!forum/envoy-dev)
  mailing list.
* If your PR involves any changes to
  [envoy-filter-example](https://github.com/envoyproxy/envoy-filter-example) (for example making a new
  branch so that CI can pass) it is your responsibility to follow through with merging those
  changes back to master once the CI dance is done.

# PR review policy for maintainers

* Typically we try to turn around reviews within one business day.
* See [OWNERS.md](OWNERS.md) for the current list of maintainers.
* It is generally expected that a senior maintainer should review every PR.
* It is also generally expected that a "domain expert" for the code the PR touches should review the
  PR. This person does not necessarily need to have commit access.
* The previous two points generally mean that every PR should have two approvals. (Exceptions can
  be made by the senior maintainers).
* The above rules may be waived for PRs which only update docs or comments, or trivial changes to
  tests and tools (where trivial is decided by the maintainer in question).
* In general, we should also attempt to make sure that at least one of the approvals is *from an
  organization different from the PR author.* E.g., if Lyft authors a PR, at least one approver
  should be from an organization other than Lyft. This helps us make sure that we aren't putting
  organization specific shortcuts into the code.
* If there is a question on who should review a PR please discuss in Slack.
* Anyone is welcome to review any PR that they want, whether they are a maintainer or not.
* Please **clean up the title and body** before merging. By default, GitHub fills the squash merge
  title with the original title, and the commit body with every individual commit from the PR.
  The maintainer doing the merge should make sure the title follows the guidelines above and should
  overwrite the body with the original extended description from the PR (cleaning it up if necessary)
  while preserving the PR author's final DCO sign-off.
* If a PR includes a deprecation/breaking change, notification should be sent to the
  [envoy-announce](https://groups.google.com/forum/#!forum/envoy-announce) email list.

# DCO: Sign your work

Envoy ships commit hooks that allow you to auto-generate the DCO signoff line if
it doesn't exist when you run `git commit`. Simply navigate to the Envoy project
root and run:

```bash
./support/bootstrap
```

From here, simply commit as normal, and you will see the signoff at the bottom
of each commit.

The sign-off is a simple line at the end of the explanation for the
patch, which certifies that you wrote it or otherwise have the right to
pass it on as an open-source patch. The rules are pretty simple: if you
can certify the below (from
[developercertificate.org](http://developercertificate.org/)):

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
practice is to [squash](http://gitready.com/advanced/2009/02/10/squashing-commits-with-rebase.html)
the commit history to a single commit, append the DCO sign-off as described above, and [force
push](https://git-scm.com/docs/git-push#git-push---force). For example, if you have 2 commits in
your history:

```bash
git rebase -i HEAD^^
(interactive squash + DCO append)
git push origin -f
```

Note, that in general rewriting history in this way is a hinderance to the review process and this
should only be done to correct a DCO mistake.
