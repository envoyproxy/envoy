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

### <a name="desc"></a>Description

The description field should include a more verbose explanation of what this PR
does. If this PR causes a change in behavior it should document the behavior
before and after   If fixing a bug, please describe what the original issue is and
how the change resolves it. If it is configuration controlled, it should note
how the feature is enabled etc...

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

### <a name="relnotes"></a>Release notes

If this change is user impacting OR extension developer impacting (filter API, etc.) you **must**
add a release note to [version_history.rst](docs/root/intro/version_history.rst). Please include
any relevant links. Each release note should be prefixed with the relevant subsystem in
**alphabetical order** (see existing examples as a guide) and include links to relevant parts of the
documentation. Thank you! Please write in N/A if there are no release notes.

### <a name="issues"></a>Issues

If this PR fixes an outstanding issue, please add a line of the form:

Fixes #Issue

This will result in the linked issue being automatically closed when the PR is
merged. If you want to associate an issue with a PR without closing the issue,
you may instead just tag the PR with the issue:

\#Issue

### <a name="deprecated"></a>Deprecated

If this PR deprecates existing Envoy APIs or code, it should include
an update to the [deprecated file](DEPRECATED.md) and a one line note in the PR
description.

If you mark existing APIs or code as deprecated, when the next release is cut, the
deprecation script will create and assign an issue to you for
cleaning up the deprecated code path.

