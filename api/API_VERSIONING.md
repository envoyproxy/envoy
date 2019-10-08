# API versioning guidelines

The Envoy project (and in the future [UDPA](https://github.com/cncf/udpa)) takes API stability and
versioning seriously. Providing stable APIs is a necessary step in ensuring API adoption and success
of the ecosystem. Below we articulate the API versioning guidelines that aim to deliver this
stability.

# API semantic versioning

The Envoy APIs consist of a family of packages, e.g. `envoy.admin.v2alpha`,
`envoy.service.trace.v2`. Each package is independently versioned with a protobuf semantic
versioning scheme based on https://cloud.google.com/apis/design/versioning.

The major version for a package is captured in its name (and directory structure). E.g. version 2
of the tracing API package is named `envoy.service.trace.v2` and its constituent protos are located
in `api/envoy/service/trace/v2`. Every protobuf must live directly in a versioned package namespace,
we do not allow subpackages such as `envoy.service.trace.v2.somethingelse`.

Minor and patch versions will be implemented in the future, this effort is tracked in
https://github.com/envoyproxy/envoy/issues/8416.

In everyday discussion and GitHub labels, we refer to the `v2`, `v3`, `vN`, `...` APIs. This has a
specific technical meaning. Any given message in the Envoy API, e.g. the `Bootstrap` at
`envoy.config.bootstrap.v3.Boostrap`, will transitively reference a number of packages in the Envoy
API. These may be at `vN`, `v(N-1)`, etc. The Envoy API is technically a DAG of versioned package
namespaces. When we talk about the `vN xDS API`, we really refer to the `N` of the root
configuration resources (e.g. bootstrap, xDS resources such as `Cluster`). The
v3 API bootstrap configuration is `envoy.config.bootstrap.v3.Boostrap`, even
though it might might transitively reference `envoy.service.trace.v2`.

# Backwards compatibility

In general, within a package's major API version, we do not allow any breaking changes. The guiding
principle is that neither the wire format nor protobuf compiler generated language bindings should
experience a backward compatible break on a change. Specifically:

* Fields should not be renumbered or have their types changed. This is standard proto development
  procedure.

* Renaming of fields or package namespaces for a proto must not occur. This is inherently dangerous,
  since:
  * Field renames break wire compatibility. This is stricter than standard proto development
    procedure in the sense that it does not break binary wire format. However, it **does** break
    loading of YAML/JSON into protos as well as text protos. Since we consider YAML/JSON to be first
    class inputs, we must not change field names.

  * For service definitions, the gRPC endpoint URL is inferred from package namespace, so this will
    break client/server communication.

  * For a message embedded in an `Any` object, the type URL, which the package namespace is a part
    of, may be used by Envoy or other API consuming code. Currently, this applies to the top-level
    resources embedded in `DiscoveryResponse` objects, e.g. `Cluster`, `Listener`, etc.

  * Consuming code will break and require source code changes to match the API changes.

* Some other changes are considered breaking for Envoy APIs that are usually considered safe in
  terms of protobuf wire compatibility:
  * Upgrading a singleton field to a repeated, e.g. `uint32 foo = 1;` to `repeated uint32 foo = 1`.
    This changes the JSON wire representation and hence is considered a breaking change.

  * Wrapping an existing field with `oneof`. This has no protobuf or JSON/YAML wire implications,
    but is disruptive to various consuming stubs in languages such as Go, creating unnecessary
    churn.

  * Increasing the strictness of
    [protoc-gen-validate](https://github.com/envoyproxy/protoc-gen-validate) annotations. Exceptions
    may be granted for scenarios in which these stricter conditions model behavior already implied
    structurally or by documentation.

The exception to the above policy is for API versions tagged `vNalpha`. Within an alpha major
version, arbitrary breaking changes are allowed.

Note that changes to default values for wrapped types, e.g. `google.protobuf.UInt32Value` are not
governed by the above policy. Any management server requiring stability across Envoy API or
implementations within a major version should set explicit values for these fields.

# API lifecycle

The API lifecycle follows a calendar clock. At the end of Q3 each year, a major API version
increment may occur for any Envoy API package, in concert with the quarterly Envoy release.

Envoy will support at most three major versions of any API package at all times:
* The current stable major version, e.g. v3.
* The previous stable major version, e.g. v2. This is needed to ensure that we provide at least 1
  year for a supported major version to sunset. By supporting two stable major versions
  simultaneously, this makes it easier to coordinate control plane and Envoy
  rollouts as well. This previous stable major version will be supported for 1
  year after the introduction of the new current stable major version.
* Optionally, the next experimental alpha major version, e.g. v4alpha. This is a release candidate
  for the next stable major version. This is only generated when the current stable major version
  requires a breaking change at the next cycle, e.g. a deprecation or field rename. This release
  candidate is mechanically generated via the
  [protoxform](https://github.com/envoyproxy/envoy/tree/master/tools/protoxform) tool from the
  current stable major version, making use of annotations such as `deprecated = true`. This is not a
  human editable artifact.

An example of how this might play out is that at the end of September in 2020, we will freeze
`envoy.config.bootstrap.v4alpha` and this package will become the current stable major version
`envoy.config.bootstrap.v4`. The `envoy.config.bootstrap.v3` package will become the previous stable
major version and support for `envoy.config.bootstrap.v2` will be dropped from the Envoy
implementation. Note that some transitively referenced package, e.g.
`envoy.config.filter.network.foo.v2` may remain at version 2 during this release, if no changes were
made to the referenced package.

The implication of this API lifecycle and clock is that any deprecated feature in the Envoy API will retain
implementation support for 1-2 years (1.5 years on average).

# New API features

The Envoy APIs can be [safely extended](https://cloud.google.com/apis/design/compatibility) with new
packages, messages, enums, fields and enum values, while maintaining [backwards
compatibility](#backwards-compatibility). Additions to the API for a given package should normally
only be made to the *current stable major version*. The rationale for this policy is that:
* The feature is immediately available to Envoy users who consume the current stable major version.
  This would not be the case if the feature was placed in `vNalpha`.
* `vNalpha` can be mechanically generated from `vN` without requiring developers to maintain the new
  feature in both locations.
* We encourage Envoy users to move to the current stable major version from the previous one to
  consume new functionality.

# When can an API change be made to a package's previous stable major version?

As a pragmatic concession, we allow API feature additions to the previous stable major version for a
single quarter following a major API version increment. Any changes to the previous stable major
version must be manually reflected in a consistent manner in the current stable major version as
well.

# How to make a breaking change across major versions

We maintain [backwards compatibility](#backwards-compatibility) within a major version but allow
breaking changes across major versions. This enables API deprecations, cleanups, refactoring and
reorganization. The Envoy APIs have a stylized workflow for achieving this. There are two prescribed
methods, depending on whether the change is mechanical or manual.

## Mechanical breaking changes

Field deprecations, renames, etc. are mechanical changes that will be supported by the
[protoxform](https://github.com/envoyproxy/envoy/tree/master/tools/protoxform) tool. These are
guided by annotations in protobuf.
* Deprecations are specified with the built-in protobuf deprecated option set on a message, enum,
  field or enum value. No field may be marked as deprecated unless a replacement for this
  functionality exists and the corresponding Envoy implementation is production ready.

* Renames are specified with a `[#rename-at-next-major-version: <new name>]` protobuf comment
  annotation.

* We anticipate that `protoxform` will also support `oneof` promotion, package movement, etc. via
  similar annotations.

## Manual breaking changes

A manual breaking change is distinct from the mechanical changes such as field deprecation, since in
general it requires new code and tests to be implemented in Envoy by hand. For example, if a developer
wants to unify `HeaderMatcher` with `StringMatcher` in the route configuration, this is a likely
candidate for this class of change. The following steps are required:
1. The new version of the feature, e.g. the `NewHeaderMatcher` message should be added, together
   with referencing fields, in the current stable major version for the route configuration proto.
2. The Envoy implementation should be changed to consume configuration from the fields added in (1).
   Translation code (and tests) should be written to map from the existing field and messages to
   (1).
3. The old message/enum/field/enum value should be annotated as deprecated.
4. At the next major version, `protoxform` will remove the deprecated version automatically.

This approach ensures that API major version releases are predictable and mechanical, and has the
bulk of the Envoy code and test changes owned by feature developers, rather than the API owners.
There will be no major `vN` initiative to address technical debt beyond that enabled by the above
process.

# One Definition Rule (ODR)

To avoid maintaining more than two stable major versions of a package, and to cope with diamond
dependency, we add a restriction on how packages may be referenced transitively; a package may have
at most one version of another package in its transitive dependency set. This implies that some
packages will have a major version bump during a release cycle simply to allow them to catch up to
the current stable version of their dependencies.

Some of this complexity and churn can be avoided by having strict rules on how packages may
reference each other. Package organization and `BUILD` visibility constraints should be used
restrictions to maintain a shallow depth in the dependency tree for any given package.

# Minimizing the impact of churn

In addition to stability, the API versioning policy has an explicit goal of minimizing the developer
overhead for the Envoy community, other clients of the APIs (e.g. gRPC), management server vendors
and the wider API tooling ecosystem. A certain amount of API churn between major versions is
desirable to reduce technical debt and to support API evolution, but too much creates costs and
barriers to upgrade.

We consider deprecations to be *mandatory changes*. Any deprecation will be removed at the next
stable API version.

Other mechanical breaking changes are considered *discretionary*. These include changes such as
field renames and are largely reflected in protobuf comments. The `protoxform` tool may decide to
minimize API churn by deferring application of discretionary changes until a major version cycle
where the respective message is undergoing a mandatory change.

The Envoy API structure helps with minimizing churn between versions. Developers should architect
and split packages such that high churn protos, e.g. HTTP connection manager, are isolated in
packages and have a shallow reference hierarchy.
