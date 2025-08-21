# RepoKitteh

## What is RepoKitteh?

<img src="https://repokitteh.io/logo.svg" height="100" align="right">

[RepoKitteh](https://repokitteh.io) is a [GitHub application](https://developer.github.com/apps/) that provides an easy way to create, integrate and maintain GitHub bots. It is deployed in GCP and supplied to Envoy under a contract with the CNCF.
The application is installed on specific GitHub repositories and interacts with these by receiving webhooks and making GitHub API calls. A root `repokitteh.star` script tells the application what to do based on the webhook received.

## Integration with Envoy
The file [repokitteh.star](https://github.com/envoyproxy/envoy/blob/main/repokitteh.star), which resides in the root of the Envoy repository tells RepoKitteh what functionality to use. The file is written in the [Starlark language](https://github.com/bazelbuild/starlark/), which is a Python dialect with well defined threading and hermeticity guarantees.

For example, the statement
```
use("github.com/repokitteh/modules/assign.star")
```
tells RepoKitteh to use the [assign.star](https://github.com/repokitteh/modules/blob/master/assign.star) module.
Similar modules can be integrated in the future into Envoy in the same way.

## Current Functionality
### [Assign](https://github.com/repokitteh/modules/blob/master/assign.star)
Set assignees to issues or pull requests.

Examples:
```
/assign @someone
```
Adds `@someone` as an assignee to the issue or pull request that this comment is made on.

```
/unassign @someone
```
Removes `@someone` as an assignee.

```
/assign-from @envoyproxy/some-team [@envoyproxy/another-team..]
```
Assign a random member from `@envoyproxy/some-team` to an issue or pull request. Additional teams may be specified as
extra args. Example teams include:
- `@envoyproxy/senior-maintainers` (Senior maintainers)
- `@envoyproxy/maintainers` (All maintainers)
- `@envoyproxy/api-shepherds` (API shepherds)
- `@envoyproxy/dependency-shepherds` (Dependency shepherds)
- `@envoyproxy/first-pass-reviewers` (Contributors that provide first-pass
  reviews, typically non-senior maintainers and maintainer applicants)

Only organization members can assign or unassign other users, who must be organization members as well.

[Demo PR](https://github.com/envoyproxy/envoybot/pull/6)

### [Review](https://github.com/repokitteh/modules/blob/master/review.star)
Requests a user to review a pull request.

Examples:
```
/review @someone
```
Asks `@someone` to review the pull requests that this comment is made on.

```
/unreview @someone
```
Removes `@someone` from the reviewers list.

Only organization members can request a review from other users or cancel it, who must be organization members as well.

[Demo PR](https://github.com/envoyproxy/envoybot/pull/7)

### [Wait](https://github.com/repokitteh/modules/blob/master/wait.star)
Wait for activity on an issue or a PR.

Example:
```
/wait
```
Sets the label `waiting` on a PR. When a new commit is pushed the label will be removed.

[Demo PR](https://github.com/envoyproxy/envoybot/pull/14)

It is also possible to wait until any comment is submitted and/or a new commit is pushed.

Example:
```
/wait-any
```
Sets the label `waiting:any` on a PR. When a new commit is pushed or any comment is submitted the label will be removed.

[Demo PR](https://github.com/envoyproxy/envoybot/pull/15)

### [Azure Pipelines Retest](https://github.com/envoyproxy/envoy/blob/main/ci/repokitteh/modules/azure_pipelines.star)
Restart failed Azure pipelines.

Example:
```
/retest
```
Restarts all failed Azure pipelines.

[Demo PR](https://github.com/envoyproxy/envoy/pull/12860#issuecomment-684832313)

### [Granular Ownerscheck](https://github.com/repokitteh/modules/blob/master/ownerscheck.star)

Two types of approvals:
1. Global approvals, done by approving the PR using Github's review approval feature.
2. Partial approval, done by commenting "/lgtm [label]" where label is the label
   associated with the path. This does not affect GitHub's PR approve status, only
   this module's maintained commit status. This approval is automatically revoked
   if any further changes are done to the relevant files in this spec.

#### Backport labeling

```
/backport
```

will labels the PR commented on with `backport/review`.

### No stalebot labeling

```
/nostalebot
```

will label the issue or PR commented on with `no stalebot`.
