# RepoKitteh

## What is RepoKitteh?

<img src="https://repokitteh.io/logo.svg" height="100" align="right">

[RepoKitteh](https://repokitteh.io) is a [GitHub application](https://developer.github.com/apps/) that provides an easy way to create, integrate and maintain GitHub bots. It is deployed in GCP and supplied to Envoy under a contract with the CNCF.
The application is installed on specific GitHub repositories and interacts with these by receiving webhooks and making GitHub API calls. A root `repokitteh.sky` script tells the application what to do based on the webhook received.

## Integration with Envoy
The file [repokitteh.sky](https://github.com/envoyproxy/envoy/blob/master/repokitteh.sky), which resides in the root of the Envoy repository tells RepoKitteh what functionality to use. The file is written in the [~~Skylark~~ Starlark language](https://github.com/bazelbuild/starlark/), which is a Python dialect with well defined threading and hermeticity guarantees.

For example, the statement
```
use("github.com/softkitteh/repokitteh-modules/assign.sky")
```
tells RepoKitteh to use the [assign.sky](https://github.com/softkitteh/repokitteh-modules/blob/master/assign.sky) module.
Similar modules can be integrated in the future into Envoy in the same way.

## Current Functionality
### [Assign](https://github.com/softkitteh/repokitteh-modules/blob/master/assign.sky)
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

Only organization members can assign or unassign other users, who must be organization members as well.

[Demo PR](https://github.com/envoyproxy/envoybot/pull/6)

### [Review](https://github.com/softkitteh/repokitteh-modules/blob/master/review.sky)
Requests a a user to recview a pull request.

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
