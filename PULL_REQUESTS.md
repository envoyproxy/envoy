When creating an Envoy pull request (PR) the text box will automatically be filled
in with the basic fields from the [pull request template](PULL_REQUEST_TEMPLATE.md). The following
is a more detailed explanation of what should go in each field.

### <a name="title"></a>Title

The title of the PR should brief (one line) noting the subsystem or the aspect this PR applies to and
explaining the overall change. Both the component and the explanation must be lower case. For example:

* ci: update build image to 44d539cb
* docs: fix indent, buffer: add copyOut() method
* router:add x-envoy-overloaded header
* tls: add support for specifying TLS session ticket keys

### <a name="desc"></a>Commit Message

The commit message field should include an explanation of what this PR
does. This will be used as the final commit message that maintainers will use to
populate the commit message when merging. If this PR causes a change in behavior
it should document the behavior before and after. If fixing a bug, please
describe what the original issue is and how the change resolves it. If it is
configuration controlled, it should note how the feature is enabled etc...


### <a name="desc"></a>Additional Description

The additional description field should include information of what this PR does
that may be out of scope for a commit message. This could include additional
information or context useful to reviewers.

### <a name="risk"></a>Risk

Risk Level is one of: Low | Medium | High

Low: Small bug fix or small optional feature.

Medium: New features that are not enabled(for example: new filter). Small-medium
features added to existing components(for example: modification to an existing
filter).

High: Complicated changes such as flow control, rewrites of critical
components, etc.

Note: The above is only a rough guide for choosing a level,
please ask if you have any concerns about the risk of the PR.

### <a name="testing"></a>Testing

The testing section should include an explanation of what testing was done, for example: unit test,
integration, manual testing, etc.

Note: It isn’t expected to do all forms of testing, please use your best judgement or ask for
guidance if you are unsure. A good rule of thumb is the riskier the change, the
more comprehensive the testing should be.

### <a name="docs"></a>Documentation

If there are documentation changes, please include a brief description of what they are. Docs
changes may be in [docs/root](docs/root) and/or inline with the API protos. Please write in
N/A if there were no documentation changes.

Any PRs with structural changes to the dataplane should also update the [Life of a
Request](https://www.envoyproxy.io/docs/envoy/latest/intro/life_of_a_request) documentation as appropriate.

### <a name="relnotes"></a>Release notes

If this change is user impacting OR extension developer impacting (filter API, etc.) you **must**
add a release note to the [version history](changelogs/current.yaml) for the
current version. Please include any relevant links. Each release note should be prefixed with the
relevant subsystem in **alphabetical order** (see existing examples as a guide) and include links
to relevant parts of the documentation. Thank you! Please write in N/A if there are no release notes.

### <a name="platform_specific_features"></a>Platform Specific Features

If this change involves any platform specific features (e.g. utilizing OS-specific socket options)
or only implements new features for a limited set of platforms (e.g. Linux amd64 only), please
include an explanation that addresses the reasoning behind this. Please also open a new tracking
issue for each platform this change is not implemented on (and link them in the PR) to enable
maintainers and contributors to triage. Reviewers will look for the change to avoid
`#ifdef <OSNAME>` and rather prefer feature guards to not enable the change on a given platform
using the build system.

### <a name="runtime_guard"></a>Runtime guard

If this PR has a user-visible behavioral change, or otherwise falls under the
guidelines for runtime guarding in the [contributing doc](CONTRIBUTING.md)
it should have a runtime guard, which should be documented both in the release
notes and here in the PR description.

For new feature additions guarded by configs, no-op refactors, docs changes etc.
this field can be disregarded and/or removed.

### <a name="issues"></a>Issues

If this PR fixes an outstanding issue, please add a line of the form:

Fixes #Issue

This will result in the linked issue being automatically closed when the PR is
merged. If you want to associate an issue with a PR without closing the issue,
you may instead just tag the PR with the issue:

\#Issue

### <a name="commit"></a>Commit

If this PR fixes or reverts a buggy commit, please add a line of the form:

Fixes commit #PR

or

Fixes commit SHA

This will allow automated tools to detect tainted commit ranges on the main branch when the PR is
merged.

### <a name="deprecated"></a>Deprecated

If this PR deprecates existing Envoy APIs or code, it should include an update to the deprecated
section of the [version history](changelogs/current.yaml) and a one line note in the
PR description.

If you mark existing APIs or code as deprecated, when the next release is cut, the
deprecation script will create and assign an issue to you for
cleaning up the deprecated code path.

### <a name="api"></a>API Changes

If this PR changes anything in the [api tree](https://github.com/envoyproxy/envoy/tree/main/api),
please read the [API Review
Checklist](https://github.com/envoyproxy/envoy/tree/main/api/review_checklist.md)
and make sure that your changes have addressed all of the considerations listed there.
Any relevant considerations should be documented under "API Considerations" in the PR description.
