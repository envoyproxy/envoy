# Envoy setec

## Setec branches

### The `main` branch (**This branch is CLOSED!**)

[![CII Best Practices](https://bestpractices.coreinfrastructure.org/projects/1266/badge)](https://bestpractices.coreinfrastructure.org/projects/1266)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/envoyproxy/envoy/badge)](https://api.securityscorecards.dev/projects/github.com/envoyproxy/envoy)
[![Azure Pipelines](https://dev.azure.com/cncf/envoy/_apis/build/status/11?branchName=main)](https://dev.azure.com/cncf/envoy/_build/latest?definitionId=11&branchName=main)
[![Fuzzing Status](https://oss-fuzz-build-logs.storage.googleapis.com/badges/envoy.svg)](https://bugs.chromium.org/p/oss-fuzz/issues/list?sort=-opened&can=1&q=proj:envoy)
[![Jenkins](https://powerci.osuosl.org/buildStatus/icon?job=build-envoy-static-master&subject=ppc64le%20build)](https://powerci.osuosl.org/job/build-envoy-static-master/)

It may be synced from time to time with Envoy `main` but it contains as the last commit any custom configurations that this repo requires.

Do not create Pull Requests -> `main` or update it other than to rebase to Envoy `main`

### The mirror branches

These contain the mirrors of the Envoy `main` and release branches.

These are automatically updated daily (currently by @phlax) and should not be changed manually.

Do not create Pull Requests against these branches - PR against the "patch" branches.

The current mirror branches are:

- [envoy/main](https://github.com/envoyproxy/envoy-setec/tree/envoy/main)
  [![Azure Pipelines](https://dev.azure.com/cncf/envoy-security/_apis/build/status/9?branchName=envoy/main)](https://dev.azure.com/cncf/envoy-security/_build/latest?definitionId=9&branchName=envoy/main)
- [envoy/1.25](https://github.com/envoyproxy/envoy-setec/tree/envoy/1.25)
  [![Azure Pipelines](https://dev.azure.com/cncf/envoy-security/_apis/build/status/9?branchName=envoy/1.25)](https://dev.azure.com/cncf/envoy-security/_build/latest?definitionId=9&branchName=envoy/1.25)
- [envoy/1.24](https://github.com/envoyproxy/envoy-setec/tree/envoy/1.24)
  [![Azure Pipelines](https://dev.azure.com/cncf/envoy-security/_apis/build/status/9?branchName=envoy/1.24)](https://dev.azure.com/cncf/envoy-security/_build/latest?definitionId=9&branchName=envoy/1.24)
- [envoy/1.23](https://github.com/envoyproxy/envoy-setec/tree/envoy/1.23)
  [![Azure Pipelines](https://dev.azure.com/cncf/envoy-security/_apis/build/status/9?branchName=envoy/1.23)](https://dev.azure.com/cncf/envoy-security/_build/latest?definitionId=9&branchName=envoy/1.23)
- [envoy/1.22](https://github.com/envoyproxy/envoy-setec/tree/envoy/1.22)
  [![Azure Pipelines](https://dev.azure.com/cncf/envoy-security/_apis/build/status/9?branchName=envoy/1.22)](https://dev.azure.com/cncf/envoy-security/_build/latest?definitionId=9&branchName=envoy/1.22)

Ideally, these branches should never fail as they are mirroring upstream Envoy branches, but in reality
due to flakes and breakages, failure can happen.

Checking the CI for these branches can be helpful as baseline information about upstream flakes and breaks.

### The patch branches

These branches carry the current patches that will be published.

The branches are rebased against Envoy release branches (ie `main`, `release/v1.25`, etc) each day.
If merge/rebase fails manual intervention is required (see below).

The branches are kept as Pull Requests so it can be seen what they will change when applied, and
the patches can be published to the embargo list before publication

You should open Pull Requests against these branches (in the first instance
[branch: patches/main](https://github.com/envoyproxy/envoy-setec/tree/patches/main),
and the others for backporting)

Currently the patch branch PRs are:

#### main:
Branch: [patches/main](https://github.com/envoyproxy/envoy-setec/tree/patches/main)

Patch PR: https://github.com/envoyproxy/envoy-setec/pull/1298

[![Azure Pipelines](https://dev.azure.com/cncf/envoy-security/_apis/build/status/9?branchName=patches/main)](https://dev.azure.com/cncf/envoy-security/_build/latest?definitionId=9&branchName=patches/main)

#### 1.26
Branch: [patches/1.26](https://github.com/envoyproxy/envoy-setec/tree/patches/1.26)

Patch PR: https://github.com/envoyproxy/envoy-setec/pull/1302

CI:[![Azure Pipelines](https://dev.azure.com/cncf/envoy-security/_apis/build/status/9?branchName=patches/1.26)](https://dev.azure.com/cncf/envoy-security/_build/latest?definitionId=9&branchName=patches/1.26)

#### 1.25
Branch: [patches/1.25](https://github.com/envoyproxy/envoy-setec/tree/patches/1.25)

Patch PR: https://github.com/envoyproxy/envoy-setec/pull/1302

CI:[![Azure Pipelines](https://dev.azure.com/cncf/envoy-security/_apis/build/status/9?branchName=patches/1.25)](https://dev.azure.com/cncf/envoy-security/_build/latest?definitionId=9&branchName=patches/1.25)

#### 1.24
Branch: [patches/1.24](https://github.com/envoyproxy/envoy-setec/tree/patches/1.24)

Patch PR: https://github.com/envoyproxy/envoy-setec/pull/1301

[![Azure Pipelines](https://dev.azure.com/cncf/envoy-security/_apis/build/status/9?branchName=patches/1.24)](https://dev.azure.com/cncf/envoy-security/_build/latest?definitionId=9&branchName=patches/1.24)


#### 1.23
Branch: [patches/1,23](https://github.com/envoyproxy/envoy-setec/tree/patches/1.23)

Patch PR: https://github.com/envoyproxy/envoy-setec/pull/1300

[![Azure Pipelines](https://dev.azure.com/cncf/envoy-security/_apis/build/status/9?branchName=patches/1.23)](https://dev.azure.com/cncf/envoy-security/_build/latest?definitionId=9&branchName=patches/1.23)

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
