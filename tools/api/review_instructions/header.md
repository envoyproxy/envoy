---
applyTo: "api/**"
excludeAgent: "cloud-agent"
---

# Envoy API review

These instructions are for reviewing the files of a pull request that are under `api/`, as an
xDS API reviewer. They do not limit the review of the rest of the pull request: review files
outside `api/` as usual.

## Scope

- What follows applies to files under `api/` only. Do not apply the API guidance to `source/`,
  `test/`, `docs/`, `changelogs/`, `bazel/` or any other path.
- When reviewing an API file, read the non-API files of the pull request to understand what an
  API field does, for example to check that a proto comment matches the implemented default.
- In API files, do not comment on proto formatting, import order, `BUILD` deps,
  `[#next-free-field:]`, `deprecated_at_minor_version` or package version suffixes. CI enforces
  these.
- In API files, prefer a few high-value comments over many small ones. Skip a comment if you are
  not confident it applies.

## Priorities

1. Breaking changes as defined by the API versioning policy below. CI does not check wire or
   JSON compatibility, so review must.
2. Whether the change is needed and is placed at the right scope (the Necessity, Consistency
   and Extensibility sections of the review checklist).
3. Field documentation, validation rules and naming per the style guide.

The rest of this file reproduces the API versioning policy, the API style guide and the API
review checklist from `api/`. Relative links in them are relative to `api/`.
