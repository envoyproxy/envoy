# Envoy setec

## High level workflow in setec

Security fixes are sensitive and their development and deployment have a well defined process in Envoy and lead to quarterly security releases which are shipped to vendors and merged back into envoyproxy.

The high level flow is as follows:

1. Ask the current [release manager](https://github.com/envoyproxy/envoy/blob/main/RELEASES.md#release-management) to update `envoy/main` and `patches/main` to mirror latest `envoyproxy/main` when you're ready to start working on a fix
2. Create a branch from `patches/main` where you'll develop the fix
3. Create a PR for this fix against `patches/main` with 1 commit, obtain approvals and ensure tests pass but _do not merge_. Merging will occur in a later step by the release manager.
4. < time elapses, more fixes are made against patches/main over the quarter-long cycle >
5. When it's time for patch release, we'll create a stack PR for each branch and cherry-pick all the fix PRs created and approved during the cycle.
6. The tarball of patches produced by step 5, will be distributed to our vendors as prior warning and for testing. When the embargo expires, we reveal the cve/advisories and immediately make prs in the envoy repo with the patch stacks

## Setec branches

### The `main` branch (**This branch is CLOSED!**)

[![CII Best Practices](https://bestpractices.coreinfrastructure.org/projects/1266/badge)](https://bestpractices.coreinfrastructure.org/projects/1266)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/envoyproxy/envoy/badge)](https://api.securityscorecards.dev/projects/github.com/envoyproxy/envoy)
[![Fuzzing Status](https://oss-fuzz-build-logs.storage.googleapis.com/badges/envoy.svg)](https://bugs.chromium.org/p/oss-fuzz/issues/list?sort=-opened&can=1&q=proj:envoy)
[![Jenkins](https://powerci.osuosl.org/buildStatus/icon?job=build-envoy-static-master&subject=ppc64le%20build)](https://powerci.osuosl.org/job/build-envoy-static-master/)

It may be synced from time to time with Envoy `main` but it contains as the last commit any custom configurations that this repo requires.

Do not create Pull Requests -> `main` or update it other than to rebase to Envoy `main`

### The mirror branches

These contain the mirrors of the Envoy `main` and release branches.

These are manually updated on-demand by the [release manager](https://github.com/envoyproxy/envoy/blob/main/RELEASES.md#release-management). If they are out of date, ask the release manager to update them.

Do not create Pull Requests against these branches - PR against the "patch" branches.

The current mirror branches are:

- [envoy/main](https://github.com/envoyproxy/envoy-setec/tree/envoy/main)

Ideally, these branches should never fail as they are mirroring upstream Envoy branches, but in reality
due to flakes and breakages, failure can happen.

Checking the CI for these branches can be helpful as baseline information about upstream flakes and breaks.

### The patch branches

These branches carry the current patches that will be published.

The branches are rebased against Envoy release branches (ie `main`, `release/v1.25`, etc) on demand.
If merge/rebase fails manual intervention is required (see below).

You should open Pull Requests against these branches (in the first instance
[branch: patches/main](https://github.com/envoyproxy/envoy-setec/tree/patches/main),
and the others for backporting)

Currently the patch branch PRs are:

#### main:
Branch: [patches/main](https://github.com/envoyproxy/envoy-setec/tree/patches/main)

Patch PR: [Find the open patch PRs here](https://github.com/envoyproxy/envoy-setec/pulls?q=is%3Apr+is%3Aopen+label%3Apatch%3Abranch)

### Resolving conflicts with upstream

If the branches cannot be automatically rebased we will need to intervene.

In this case the related patch PR will show a merge conflict.

To resolve, open a pull request against the branch, merge the related `envoy/...` branch and resolve
the conflict.

For example create a PR against [branch: patches/main](https://github.com/envoyproxy/envoy-setec/tree/patches/main),
which merges [branch: envoy/main](https://github.com/envoyproxy/envoy-setec/tree/envoy/main).

It is Envoy `main` that changes the most and is most likely to create conflict

The other type of failure is where the merge applies cleanly but breaks tests - in this case CI for the
patch branch will be failing - and again open a PR against the patch branch to fix.

When landing update PRs to patch branches (ie a fix for an existing patch) it is generally better to
squash the fix to the relevant patch commit as this will make it easier to resolve any further
conflict, and retain clean patches for applying on release.

For this reason the patch branches are rebased/force pushed and should be regarded as volatile.

### Linting `main` branch

The `main` branch is linted using Envoy's `envoy.code.check`.

This runs the following checks:

- glint (general file sanity)
- yamllint (yaml formatting/linting)
- flake8 (python formatting/linting)

You can run the linter locally with:

```console
$ pip install envoy.code.check
$ envoy.code.check . -c glint python_flake8 yamllint
```
