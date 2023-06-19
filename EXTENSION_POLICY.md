# Envoy Extension Policy

## Quality requirements

All extensions contained in the main Envoy repository will be held to the same quality bar as the
core Envoy code. This includes coding style, code reviews, test coverage, etc. In the future we
may consider creating a sandbox repository for extensions that are not compiled/tested by default
and held to a lower quality standard, but that is out of scope currently.

## Adding new extensions

The following procedure will be used when proposing new extensions for inclusion in the repository:
  1. A GitHub issue should be opened describing the proposed extension as with any major feature
  proposal.
  2. All extensions must be sponsored by an existing maintainer. Sponsorship means that the
  maintainer will shepherd the extension through design/code reviews. Maintainers can self-sponsor
  extensions if they are going to write them, shepherd them, and maintain them.

     Sponsorship serves two purposes:
     * It ensures that the extension will ultimately meet the Envoy quality bar.
     * It makes sure that incentives are aligned and that extensions are not added to the repo without
     sufficient thought put into future maintenance.

     *If sponsorship cannot be found from an existing maintainer, an organization can consider
     [doing the work to become a maintainer](./GOVERNANCE.md#process-for-becoming-a-maintainer) in
     order to be able to self-sponsor extensions.*

  3. Each extension must have two reviewers proposed for reviewing PRs to the extension. Neither of
  the reviewers must be a senior maintainer. Existing maintainers (including the sponsor) and other
  contributors can count towards this number. The initial reviewers will be codified in the
  [CODEOWNERS](./CODEOWNERS) file for long term maintenance. These reviewers can be swapped out as
  needed.
  4. Any extension added via this process becomes a full part of the repository. This means that any
  API breaking changes in the core code will be automatically fixed as part of the normal PR process
  by other contributors.
  5. Any new dependencies added for this extension must comply with
  [DEPENDENCY_POLICY.md](DEPENDENCY_POLICY.md), please follow the steps detailed there.
  6. If an extension depends on platform specific functionality, be sure to guard it in the build
  system. See [platform specific features](./PULL_REQUESTS.md#platform-specific-features).
  Add the extension to the necessary `*_SKIP_TARGETS` in [bazel/repositories.bzl](bazel/repositories.bzl)
  and tag tests to be skipped/failed on the unsupported platform.

## Removing existing extensions

As stated in the previous section, once an extension becomes part of the repository it will be
maintained by the collective set of Envoy contributors as needed.

However, if an extension has known issues that are not being rectified by the original sponsor and
reviewers or new contributors that are willing to step into the role of extension owner, a
[vote of the maintainers](./GOVERNANCE.md#conflict-resolution-and-voting) can be called to remove the
extension from the repository.

## Extension pull request reviews

Extension PRs must not modify core Envoy code. In the event that an extension requires changes to core
Envoy code, those changes should be submitted as a separate PR and will undergo the normal code review
process, as documented in the [contributor's guide](./CONTRIBUTING.md).

Extension PRs must be approved by at least one sponsoring maintainer and an extension reviewer. These
may be a single individual, but it is always preferred to have multiple reviewers when feasible.

In the event that the Extension PR author is a sponsoring maintainer and no other sponsoring maintainer
is available, another maintainer may be enlisted to perform a minimal review for style and common C++
anti-patterns. The Extension PR must still be approved by a non-maintainer reviewer.

## Wasm extensions

Wasm extensions are not allowed in the main envoyproxy/envoy repository unless
part of the Wasm implementation validation. The rationale for this policy:
* Wasm extensions should not depend upon Envoy implementation specifics as
  they exist behind a version independent ABI. Hence, there is little value in
  qualifying Wasm extensions in the main repository.
* Wasm extensions introduce extensive dependencies via crates, etc. We would
  prefer to keep the envoyproxy/envoy repository dependencies minimal, easy
  to reason about and maintain.
* We do not implement any core extensions in Wasm and do not plan to in the
  medium term.

## Extension stability and security posture

Every extension is expected to be tagged with a `status` and `security_posture` in its
`envoy_cc_extension` rule.

The `status` is one of:
* `stable`: The extension is stable and is expected to be production usable. This is the default if
  no `status` is specified.
* `alpha`: The extension is functional but has not had substantial production burn time, use only
  with this caveat.
* `wip`: The extension is work-in-progress. Functionality is incomplete and it is not intended for
  production use.

The extension status may be adjusted by the extension [CODEOWNERS](./CODEOWNERS) and/or Envoy
maintainers based on an assessment of the above criteria. Note that the status of the extension
reflects the implementation status. It is orthogonal to the API stability, for example, an extension
API marked with `(xds.annotations.v3.file_status).work_in_progress` might have a `stable` implementation and
and an extension with a stable config proto can have a `wip` implementation.

The `security_posture` is one of:
* `robust_to_untrusted_downstream`: The extension is hardened against untrusted downstream traffic. It
   assumes that the upstream is trusted.
* `robust_to_untrusted_downstream_and_upstream`: The extension is hardened against both untrusted
   downstream and upstream traffic.
* `requires_trusted_downstream_and_upstream`: The extension is not hardened and should only be used in deployments
   where both the downstream and upstream are trusted.
* `unknown`: This is functionally equivalent to `requires_trusted_downstream_and_upstream`, but acts
  as a placeholder to allow us to identify extensions that need classifying.
* `data_plane_agnostic`: Not relevant to data plane threats, e.g. stats sinks.

An assessment of a robust security posture for an extension is subject to the following guidelines:

* Does the extension have fuzz coverage? If it's only receiving fuzzing
  courtesy of the generic listener/network/HTTP filter fuzzers, does it have a
  dedicated fuzzer for any parts of the code that would benefit?
* Does the extension have unbounded internal buffering? Does it participate in
  flow control via watermarking as needed?
* Does the extension have at least one deployment with live untrusted traffic
  for a period of time, N months?
* Does the extension rely on dependencies that meet our [extension maturity
  model](https://github.com/envoyproxy/envoy/issues/10471)?
* Is the extension reasonable to audit by Envoy security team?
* Is the extension free of obvious scary things, e.g. `memcpy`, does it have gnarly parsing code, etc?
* Does the extension have active [CODEOWNERS](CODEOWNERS) who are willing to
  vouch for the robustness of the extension?
* Is the extension absent a [low coverage
  exception](https://github.com/envoyproxy/envoy/blob/main/test/per_file_coverage.sh#L5)?

The current stability and security posture of all extensions can be seen
[here](https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/security/threat_model#core-and-extensions).

## Adding Extension Points

Envoy might lack the extension point necessary for an extension. In that
case we need to install an extension point, which can be done as follows:

  1. Open a GitHub issue describing the proposed extension point and use cases.
  2. Make changes in core Envoy for the extension point.
  3. Update [extending envoy](docs/root/extending/extending.rst) to list the new
     extension point and add any documentation explaining the extension point.
     At the very least this should link to the corresponding proto.

## Contrib extensions

As described in [this document](https://docs.google.com/document/d/1yl7GOZK1TDm_7vxQvt8UQEAu07UQFru1uEKXM6ZZg_g/edit#),
Envoy allows an alternate path to adding extensions called `contrib/`. The barrier to entry for a
contrib extension is lower than a core extension, with the tradeoff that contrib extensions are not
included by default in the main image builds. Consumers need to pull directly from the contrib
images described in the installation guide. Please read the linked document in detail to determine
whether contrib extensions are the right choice for a newly proposed extension.

**NOTE:** Contrib extensions **require** an end-user sponsor. The sponsor is someone who will run
the extension at sufficient scale as to make the build maintenance and other overhead worthwhile.
The definition of "sufficient scale" is up to the maintainers and can change at any time. The
end-user sponsor *does not* have to author the extension, but the end-user sponsor will need to make
an "on the record" attestation of their planned usage of the extension. This attestation should
occur in a GitHub issue opened to discuss the new extension. In this context "end user" has the
same definition as the one specified in the [security policy](SECURITY.md#membership-criteria)
membership criteria (point 1.3.5).

**NOTE:** Contrib extensions are not eligible for Envoy security team coverage.

**NOTE:** As per the linked Google Doc, contrib extensions generally should use `v3alpha` to avoid
requiring API shepherd reviews.
