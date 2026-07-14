# API versioning guidelines

The Envoy project and [xDS working group](https://github.com/cncf/xds) take API stability and
versioning seriously. Providing stable APIs is a necessary step in ensuring API adoption and success
of the ecosystem. Below we articulate the API versioning guidelines that aim to deliver this
stability.

# API semantic versioning

The Envoy APIs consist of a family of packages, e.g. `envoy.admin.v3alpha`,
`envoy.service.trace.v3`. Each package is independently versioned with a protobuf semantic
versioning scheme based on https://cloud.google.com/apis/design/versioning.

The major version for a package is captured in its name (and directory structure). E.g. version 3
of the tracing API package is named `envoy.service.trace.v3` and its constituent protos are located
in `api/envoy/service/trace/v3`. Every protobuf must live directly in a versioned package namespace,
we do not allow subpackages such as `envoy.service.trace.v3.somethingelse`.

In everyday discussion and GitHub labels, we refer to the `v2`, `v3`, `vN`, `...` APIs. This has a
specific technical meaning. Any given message in the Envoy API, e.g. the `Bootstrap` at
`envoy.config.bootstrap.v3.Bootstrap`, will transitively reference a number of packages in the Envoy
API. These may be at `vN`, `v(N-1)`, etc. The Envoy API is technically a DAG of versioned package
namespaces. When we talk about the `vN xDS API`, we really refer to the `N` of the root
configuration resources (e.g. bootstrap, xDS resources such as `Cluster`). The
v3 API bootstrap configuration is `envoy.config.bootstrap.v3.Bootstrap`.

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
    [protoc-gen-validate](https://github.com/bufbuild/protoc-gen-validate) annotations. Exceptions
    may be granted for scenarios in which these stricter conditions model behavior already implied
    structurally or by documentation.

An exception to the above policy exists for:
* Changes made within 14 days of the introduction of a new API field or message, provided the new field
or message has not been included in an Envoy release.
* API versions tagged `vNalpha`. Within an alpha major version, arbitrary breaking changes are allowed.
* Any proto with a `(udpa.annotations.file_status).work_in_progress`,
  `(xds.annotations.v3.file_status).work_in_progress`
  `(xds.annotations.v3.message_status).work_in_progress`, or
  `(xds.annotations.v3.field_status).work_in_progress` option annotation.

Note that changes to default values for wrapped types, e.g. `google.protobuf.UInt32Value` are not
governed by the above policy. Any management server requiring stability across Envoy API or
implementations within a major version should set explicit values for these fields.

# API lifecycle

At one point, the Envoy project planned for regular major version updates to the xDS API in order to
remove technical debt. At this point we recognize that Envoy and the larger xDS ecosystem (gRPC,
etc.) is so widely used that version bumps are no longer realistic. As such, for practical purposes,
the v3 API is the final major version of the API and will be supported forever. Deprecations will
still occur as an end-user indication that there is a preferred way to configure a particular feature,
but no field will ever be removed nor will Envoy ever remove the implementation for any deprecated
field.

**NOTE**: Client implementations are free to output additional warnings about field usage beyond
deprecation, if for example, the continued use of the field is deemed a substantial
security risk. Individual client versions are also free to stop supporting fields if they want to,
though Envoy Proxy (as an xDS client) commits to never doing so.

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

# Client features

Not all clients will support all fields and features in a given major API version. In general, it is
preferable to use Protobuf semantics to support this, for example:
* Ignoring a field's contents is sufficient to indicate that the support is missing in a client.
* Setting both deprecated and the new method for expressing a field if support for a range of
  clients is desired (where this does not involve huge overhead or gymnastics).

This approach does not always work, for example:
* A route matcher conjunct condition should not be ignored just because the client is missing the
  ability to implement the match; this might result in route policy bypass.
* A client may expect the server to provide a response in a certain format or encoding, for example
  a JSON encoded `Struct`-in-`Any` representation of opaque extension configuration.

For this purpose, we have [client
features](https://www.envoyproxy.io/docs/envoy/latest/api/client_features).

