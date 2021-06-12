# Envoy External Dependency Policy

Envoy has an evolving policy on external dependencies, tracked at
https://github.com/envoyproxy/envoy/issues/10471. This will become stricter over time, below we
detail the policy as it currently applies.

## External dependencies dashboard

The list of external dependencies in Envoy with their current version is available at
https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/security/external_deps

## Declaring external dependencies

In general, all external dependencies for the Envoy proxy binary build and test should be declared
in either [bazel/repository_locations.bzl](bazel/repository_locations.bzl) or
[api/bazel/repository_locations.bzl](api/bazel/repository_locations.bzl), unless listed under
[policy exceptions](#policy-exceptions).

An example entry for the `nghttp2` dependency is:

```python
com_github_nghttp2_nghttp2 = dict(
    project_name = "Nghttp2",
    project_desc = "Implementation of HTTP/2 and its header compression ...",
    project_url = "https://nghttp2.org",
    version = "1.41.0",
    sha256 = "eacc6f0f8543583ecd659faf0a3f906ed03826f1d4157b536b4b385fe47c5bb8",
    strip_prefix = "nghttp2-{version}",
    urls = ["https://github.com/nghttp2/nghttp2/releases/download/v{version}/nghttp2-{version}.tar.gz"],
    use_category = ["dataplane"],
    last_updated = "2020-06-02",
    cpe = "cpe:2.3:a:nghttp2:nghttp2:*",
),
```

Dependency declarations must:

* Provide a meaningful project name and URL.
* State the version in the `version` field. String interpolation should be used in `strip_prefix`
  and `urls` to reference the version. If you need to reference version `X.Y.Z` as `X_Y_Z`, this
  may appear in a string as `{underscore_version}`, similarly for `X-Y-Z` you can use
  `{dash_version}`.
* Versions should prefer release versions over main branch GitHub SHA tarballs. A comment is
  necessary if the latter is used. This comment should contain the reason that a non-release
  version is being used.
* Provide accurate entries for `use_category`. Please think carefully about whether there are data
  or control plane implications of the dependency.
* Reflect the UTC date (YYYY-MM-DD format) for the dependency release. This is when
  the dependency was updated in its repository. For dependencies that have
  releases, this is the date of the release. For dependencies without releases
  or for scenarios where we temporarily need to use a commit, this date should
  be the date of the commit in UTC.
* CPEs are compulsory for all dependencies that are not purely build/test.
  [CPEs](https://en.wikipedia.org/wiki/Common_Platform_Enumeration) provide metadata that allow us
  to correlate with related CVEs in dashboards and other tooling, and also provide a machine
  consumable join key. You can consult [CPE
  search](https://nvd.nist.gov/products/cpe/search) to find a CPE for a dependency.`"N/A"` should only
  be used if no CPE for the project is available in the CPE database. CPEs should be _versionless_
  with a `:*` suffix, since the version can be computed from `version`.

When build or test code references Python modules, they should be specified via `pip_install` in
[bazel/repositories_extra.bzl](bazel/repositories_extra.bzl). Python modules should not be listed in
`repository_locations.bzl` entries. `requirements.txt` files for Python dependencies must pin to
exact versions, e.g. `PyYAML==5.4.1` and ideally also include a [SHA256
checksum](https://davidwalsh.name/hashin).

Pure developer tooling and documentation builds may reference Python via standalone
`requirements.txt`, following the above policy.

## New external dependencies

Any new dependency on the Envoy data or control plane that impacts Envoy core (i.e. is not
specific to a single non-core extension) must be cleared with the Envoy dependency shepherds and
security team, please file an issue and tag both [dependency
shepherds](https://github.com/orgs/envoyproxy/teams/dependency-shepherds) and
the [@envoyproxy/security-team](https://github.com/orgs/envoyproxy/teams/security-team).

The criteria below are used to evaluate new dependencies on the data, control
and observability plane. They apply to all core dependencies and any extension
that is robust to untrusted downstream or upstream traffic. The criteria are
guidelines, exceptions may be granted with solid rationale. Precedent from
existing extensions does not apply; there are extant extensions in violation of
this policy which we will be addressing over time, they do not provide grounds
to ignore policy criteria below.

|Criteria|Requirement|Mnemonic|Weight|Rationale|
|--------|-----------|--------|------|---------|
|Cloud Native Computing Foundation (CNCF) [approved license](https://github.com/cncf/foundation/blob/master/allowed-third-party-license-policy.md#approved-licenses-for-allowlist)|MUST|License|High||
|Dependencies must not substantially increase the binary size unless they are optional (i.e. confined to specific extensions)|MUST|BinarySize|High|Envoy Mobile is sensitive to binary size. We should pick dependencies that are used in core with this criteria in mind.|
|No duplication of existing dependencies|MUST|NoDuplication|High|Avoid maintenance cost of multiple JSON parsers etc|
|Hosted on a git repository and the archive fetch must directly reference this repository. We will NOT support intermediate artifacts built by-hand located on GCS, S3, etc.|MUST|Source|High|Flows based on manual updates are fragile (they are not tested until needed), often suffer from missing documentation and shared exercise, may fail during emergency zero day updates and have no audit trail (i.e. it's unclear how the artifact we depend upon came to be at a later date).|
|CVE history appears reasonable, no pathological CVE arcs|MUST|SoundCVEs|High|Avoid dependencies that are CVE heavy in the same area (e.g. buffer overflow)
|Code review (ideally PRs) before merge|MUST|Code-Review|Normal|Consistent code reviews|
|Security vulnerability process exists, with contact details and reporting/disclosure process|MUST|SecPolicy|High|Lack of a policy implies security bugs are open zero days|
|> 1 contributor responsible for a non-trivial number of commits|MUST|Contributors|Normal|Avoid bus factor of 1|
|Tests run in CI|MUST|CI-Tests|Normal|Changes gated on tests|
|High test coverage (also static/dynamic analysis, fuzzing)|SHOULD|Test-Coverage|Normal|Key dependencies must meet the same quality bar as Envoy|
|Envoy can obtain advanced notification of vulnerabilities or of security releases|SHOULD|SecPolicy-Compat|High|Coordinated security releases possible, but most dependencies do not feature this.|
|Do other significant projects have shared fate by using this dependency?|SHOULD|SharedFate|High|Increased likelihood of security community interest, many eyes.|
|Releases (with release notes)|SHOULD|Releases|Normal|Discrete upgrade points, clear understanding of security implications. We have many counterexamples today (e.g. CEL, re2).|
|Commits/releases in last 90 days|SHOULD|Active|Normal|Avoid unmaintained deps, not compulsory since some code bases are “done”|

The rationale behind this policy is tracked
[here](https://docs.google.com/document/d/1HbREo7pv7rgeIIjQn6mNpySzQE5rx2Yv9dXm5NqR2N8/edit#).

## Maintaining existing dependencies

We rely on community volunteers to help track the latest versions of dependencies. On a best effort
basis:

* Core Envoy dependencies will be updated by the Envoy maintainers/security team.

* Extension [CODEOWNERS](CODEOWNERS) should update extension specific dependencies.

Where possible, we prefer the latest release version for external dependencies, rather than main
branch GitHub SHA tarballs.

## Dependency shepherds

Sign-off from the [dependency
shepherds](https://github.com/orgs/envoyproxy/teams/dependency-shepherds) is
required for every PR that modifies external dependencies. The shepherds will
look to see that the policy in this document is enforced and that metadata is
kept up-to-date.

## Dependency patches

Occasionally it is necessary to introduce an Envoy-side patch to a dependency in a `.patch` file.
These are typically applied in [bazel/repositories.bzl](bazel/repositories.bzl). Our policy on this
is as follows:

* Patch files impede dependency updates. They are expedient at creation time but are a maintenance
  penalty. They reduce the velocity and increase the effort of upgrades in response to security
  vulnerabilities in external dependencies.

* No patch will be accepted without a sincere and sustained effort to upstream the patch to the
  dependency's canonical repository.

* There should exist a plan-of-record, filed as an issue in Envoy or the upstream GitHub tracking
  elimination of the patch.

* Every patch must have comments at its point-of-use in [bazel/repositories.bzl](bazel/repositories.bzl)
  providing a rationale and detailing the tracking issue.

## Policy exceptions

The following dependencies are exempt from the policy:

* Any developer-only facing tooling or the documentation build.

* Transitive build time dependencies, e.g. Go projects vendored into
  [protoc-gen-validate](https://github.com/envoyproxy/protoc-gen-validate).
