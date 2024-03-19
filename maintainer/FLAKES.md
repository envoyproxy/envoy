
# Finding, reporting and fixing flakes in Envoy's CI

Flakes cause Pull Requests, builds, and release CI to fail, wasting a large
amount of developer time and CI cycles.

When CI is sufficiently flakey, it becomes easy to ignore new ones, and hard
to debug real fails.

More often than not, the solution will be reasonably obvious to the author of
the PR that has introduced the flake, so the quicker this can be brought to their
attention, the more likely it is that it can be fixed easily.

## Types of flake

### Tests marked flaky

Envoy's bazel has `--flaky_test_attempts` set for much of its CI.

These flakes are not so common, but are reported to the #flakey-test channel in Slack.

### Tests that just flake

The more common type of flake is a test that simply does or does not fail CI
in different test runs.

### Flakes caused by external issues

Another cause of flakes is upstream issues, usually Github or Azure pipelines.

For example `apt` mirrors may fail, dockerhub may fail pulling images, and so on.

While we are always looking for ways to mitigate this, these are generally beyond
our control, and outside the scope of this document.

## Flake hunting

You often don't need to find flakes, they find you.

The key thing is not to ignore them to ensure that they are addressed.

Here are some tips for identifying and actively finding flaking tests.

### CI runs that respond to a `/retest`

Any time a test has failed and issuing a `/retest` resolves then this
indicates that something has flaked CI.

### Failing `postsubmit` CI

In an ideal world `postsubmit` should never fail.

In reality it fails pretty often, and the causes of this are generally flaking tests.

Checking the `postsubmit` queue is generally a good place to start looking for flakes.

### Failing release branch CI

Release branches are even more prone to failure for various reasons.

This can make them useful, especially on a recent release branch, for finding flakes.

The caveat is that the issues there may no longer be present on `main`, but this
can be useful for spotting longer term, less frequent, flakes.

### AZP CI test analytics

This is probably the most useful way to find flaking tests.

It is not the best designed UI/UX but with a bit of patience it can be used to glean
the useful information.

Generally it is better to be checking the analytics for `postsubmit`, as `presubmit`
is expected to fail sometimes but it can be useful when tracking a flaking test
to see when it was first seen there, or for patterns of occurrence.

To view the AZP `postsubmit` analytics browse to https://dev.azure.com/cncf/envoy/_test/analytics?definitionId=11&contextType=build

The first thing you will most likely want to do is to filter the branch to `refs/head/main`.

By setting different time scales and/or filtering on the "Test file" you can generally spot when
a flaking test has first occurred.

Clicking through to the test, filtering the outcome on "Failed", scrolling to the bottom edge,
and double-clicking on the bottom half-covered link, will let you see the first test run that failed.

## Reporting flakes

It is very important that we surface information about failing tests as quickly as possible.

Even if you do not have the time to debug the flake, it is essential to make it known.

The key bits of information when reporting a flake:

- The specific test that is failing - preferably put this in the title so it is obvious and easy to search for
- A snippet example of failing CI console log
- A link to an example of failing CI
- When the flake first occurred (if known)
- Frequency (if known)
- Locally reproducible (if attempted)
- Likely PRs that may have caused the change (if known)
- Contributors that may have more context on the problem/solution (if known)

On call maintainers should fill in missing information for incoming flake reports where possible.

## Debugging flakes

If you are the on-call maintainer, or have the cycles/motivation to follow up on an identified
flake, here are some tips for doing so.

### Running the failing test locally

With the caveat that a flake not failing does not disprove its existence, this can
be the most useful way of confirming the problem, and gathering further information.

When flake hunting local reproducibility is gold.

### Identifying candidate Pull Requests

This can be an imprecise art so the more information you can glean first the better.

The key bit of information you need is when it first occurred.

Once you have that you can check the git log to try and spot something that changed
or impacted relevant code.

TIP: set `git config log.date local` to view the log with normalized timezones.

If you are familiar with the code in question, or if there are some obviously
relevant code changes, then git blame or the blame ui in github can be very useful
for tracking what changed.

### Confirming a candidate Pull Requests

Once you have a suspect, and if the issue is locally reproducible, you can either
try to revert that commit, or reset your branch to the commit's parent, and rerun
the failing test.
