## CI configuration

CI is configured in .github/config.yml.

The configuration is per-branch and in this way different branches can have a different
runtime configuration.

In a pull request only 2 things are read from the config.yml submitted in the request:

- version
- build image

As these can change the way the CI runs they are allowed to change. No other configuration
is read from the pull request itself.

### Checks

Which checks should run against a commit or PR is configured under the `checks` key.

The names of these checks should match any checks that are set to required for the repo,
and if a check is required this should be set in the config to ensure the check is marked
as skipped if the related runs are skipped.

### Runs

This controls which workflows run, and where necessary which jobs in the workflows.

This paths can be configured with glob matches to match changed files.

Paths are always matched for PRs.

For push requests the config can be set to:

- always (default): Always runs
- paths: Runs when paths match
- never: Doesnt run on pushes

## CI requests

### All CI is requested

Whether triggered by push event or a pull_request all CI should be viewed as "requested".

This is very important as it means we can treat incoming triggers in much the same way
as we might handle an incoming web request.

Much like a web request, CI requests may be "trusted" or "untrusted" and as a consequence
have more or less capability or access.

Again, much like web requests, CI requests cannot be assumed to be safe.

Any incoming data - critically data over which a user has the capability to change should
be treated in the same way that user data is handled in a web request.

Failure to do this opens our CI up to many of the same attacks you might expect in a web scenario
- mostly injection attacks of various sorts.

### Requests are always made _from_ the triggering branch

The only CI workflow that is required/used on any branch other than `main` is `request.yml`.

This file contains any custom configurations required by the branch - for example, build images.

The request workflow on any branch always delegates to the `_request.yml` on `main`.

The `_request.yml` workflow contains all required configuration for handling an incoming request.

All other CI listens for the request workflow to run, and then runs with the requested/parsed data.

### CI is always run _in_ the context of main

Other than updating configurations in any given `request.yml` - no CI workflows are parsed
anywhere other than in the context of `main`.

This means that **all** changes must be made to the `main` workflows for _any_ branch _and_ for PRs.

Like branch CI, PRs also run in the context of `main` - making changes to these files in a PR will have
no effect until/unless they are landed on the `main` branch.

### Lifecycle of a CI request

#### Incoming request:

Requests can be triggered by a  `push` to `main` or a release branch or from a
`pull_request_target` to those branches.

The `request.yml` file handles this and *must* live on every branch.

This wf then calls the reusable `_request.yml` workflow, typically on `main`, but
branches can pin this if required.

#### Request is handled by `_request.yml` workflow:

This workflow initially reads the `.github/config.yml` from the target branch.

It uses this to decide which CI and which checks need to be run, and collects information
about the CI request.

This can be configured on a per-branch basis, by editing the file on the branch.

This also holds the authoritative build image information.

Users can request a CI run in a PR with custom build images by editing the config.yml file
on the relevant branch. CI will allow this but flag the change.

Likewise the version is checked at this stage, and CI flags if it has changed.

No other CI vars should be editable by users in a PR.

#### CI check runs *on main* listen for incoming requests and run if required:

These checks *always* run on `main` but with the repo checked out for the branch or the PR.

If branches require custom CI this can be added in the relevant file *on main* with
a condition to only trigger for relevant target branch.

#### Checks are completed at the end of each CI run:

Currently this reports only on the overall outcome of the CI run and updates the check.

We can add eg Slack reporting here to notify on failed `main` runs.

#### Retesting

PR CI can be retested by issuing `/retest` on the PR.

This finds the checks related to the latest request and restarts them if they are
failed or cancelled.

Links on the request page link to the original checks, but the checks themselves will
offer a `reload` button to refresh to the latest version.

## Branch CI

All CI is run on `main` - branch CI included.

The CI will checkout the correct commits and run the CI at that point.

This means that the CI on `main` should always be able to run the current supported branches.

There are possible workaround for custom branch CI but the better path is to ensure legacy support
in current `main` or backport any required changes.

## CI caching

Currently only x86 Docker images are cached.

Github has a hard per-repo limit of 10GB cache for CI which is LRU cycled when exceeded.

This should just be enough to store x86 and arm Docker images for most of our release branches
but will not leave anything to spare.

We can probably set up a bucket cache for bazel and other caching but this will need to be
done separately for un/trusted CI.

### Cache mutex

Due to shortcomings in Github's concurrency algorithm we are using a mutex lock that
is currently stored in the (private) https://github.com/envoyproxy/ci-mutex repository.

The lock allows CI jobs to wait while the cache is being primed rather than all jobs attempting
to prime the cache simultaneously.

## Development, testing and CI

Any Github workflows that use the repository context (`pull_request_target`, `workflow_run`, etc)
**are not tested in Pull Requests**

This means that changes to CI must be tested/verified in the (private) staging repository.

### CI enabling vars

The CI workflows and actions are receptive to certain environment variables being set.

`ENVOY_CI`: this allows CI to run in non-`envoyproxy/envoy` repos
`ENVOY_MOBILE_CI`: this allows mobile CI to be run in non-`envoyproxy/envoy` repos
`ENVOY_MACOS_CI`: this allows macOS CI to be run in non-`envoyproxy/envoy` repos
`ENVOY_WINDOWS_CI`: this allows Windows CI to be run in non-`envoyproxy/envoy` repos

With these flags activated the CI runs will respect the normal conditions for running.

### CI override vars

The CI workflows will also trigger for specific run settings.

For example:

`ENVOY_CI_RUN_MOBILE_ANDROID` would trigger the android CI irrespective of files changed, etc.

These correspond to the run names as configured in config.yml - for example:

`ENVOY_CI_RUN_BUILD_MACOS` would ensure the `build-macos` run is triggered.

### Debugging CI

Setting `CI_DEBUG` will provide a large amount of runtime information.

Generally this does not want to be set in a production context.
