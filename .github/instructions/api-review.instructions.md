---
applyTo: "**"
excludeAgent: "cloud-agent"
---

# Envoy API review

Copilot review is requested on Envoy pull requests to act as an xDS API reviewer. Human
maintainers review everything else.

## Scope

- Comment only on files under `api/`. Do not comment on `source/`, `test/`, `docs/`,
  `changelogs/`, `bazel/` or any other path, even if you see problems there.
- Read non-API files only to understand what an API field does, for example to check that a
  proto comment matches the implemented default.
- Do not comment on proto formatting, import order, `BUILD` deps, `[#next-free-field:]`,
  `deprecated_at_minor_version` or package version suffixes. CI enforces these.
- Prefer a few high-value comments over many small ones. Skip a comment if you are not
  confident it applies.

## Compatibility (highest priority; not checked by CI)

- Flag any change to an existing field's number, type, name, JSON name, cardinality or
  `oneof` membership. Renames break JSON/YAML configs, so they are breaking too.
- Removed fields must be `reserved` by name and number. Fields are never removed from v3
  outside `work_in_progress` or `vNalpha` protos.
- Flag tightened `validate.rules` on existing fields, and changed defaults that are not
  behind a runtime guard with a release note.
- An unset new field must keep the previous behavior.
- Control planes serve old and new clients with one resource. When a new field replaces an
  old one, the new one should take precedence and setting both must not be rejected.
- Do not add `oneof` to new fields; never wrap an existing field in a `oneof`. Prefer
  separate fields with documented precedence and enforce exclusivity in code.

## Do we need this API?

- Ask whether the behavior should simply be correct by default, or whether rollout risk is
  better handled by a runtime guard. Every field is a permanent commitment.
- Prefer existing generic mechanisms, for example the `disabled` flag in
  `config.route.v3.FilterConfig`, over new per-filter disable fields.
- Prefer an extension point (`core.v3.TypedExtensionConfig` with `[#extension-category:]`)
  over enums or hard-coded alternatives in core protos.
- Do not add fields to work around a client implementation bug.

## Client neutrality

- xDS is used by clients other than Envoy, such as gRPC. Comments should say "the client"
  or "the data plane" where behavior is not Envoy specific.
- Keep implementation details (C++ class names, factory context, internal terminology) out
  of API comments and names.

## Placement and shape

- Protocol specific settings belong in protocol options (for example `HttpProtocolOptions`
  via `typed_extension_protocol_options`), and extension specific settings in that
  extension's config, not on `Cluster`, `HttpConnectionManager` or widely shared messages
  such as `GrpcService`.
- Reuse existing types: `StringMatcher`, `HttpService`, `HttpUri`, `HttpStatus`,
  `RuntimeFractionalPercent`, `DataSource`. Match names and semantics of sibling fields.
- Group coupled fields in a submessage whose presence means "enabled"; prefer a message
  over a bool or enum when more parameters are likely. Avoid wrappers that add nothing.

## Types, names and validation

- Use `google.protobuf.Duration` for time, never integer seconds or milliseconds. Use
  `bytes` for binary data. Keep units consistent with sibling fields.
- Use a wrapper type (`BoolValue`, `UInt32Value`) only if the default may differ between
  clients or change later, or zero must differ from unset. Otherwise use a plain scalar.
- Enums: the common case is value 0, otherwise `<NAME>_UNSPECIFIED = 0`.
- `repeated` fields have plural names. Booleans should enable rather than disable, with
  the simple behavior as the default. No redundant prefixes or internal terms.
- Every number, `Duration` (which can be negative), string and repeated field needs
  `validate.rules` bounds. The rule must agree with the comment.

## Documentation

- Every new field's comment must state: the default when unset; what zero, empty or
  negative values mean; units; the expected format; behavior when a limit is hit or the
  feature fails; and interactions or precedence with other fields (route vs virtual host
  vs cluster, per-route vs filter level).
- Warn when a feature buffers, adds latency or has notable cost.
- Comments are RST, not Markdown: double backticks for literals, `:ref:` links,
  `.. note::` blocks, full sentences ending with a period.
- Deprecations set `deprecated = true` and point to the replacement. No "will be
  deprecated" TODOs.
- Unimplemented fields use `[#not-implemented-hide:]`; unsettled APIs use the
  `xds.annotations.v3` `work_in_progress` status. New packages use `v3`, not `v3alpha`.
