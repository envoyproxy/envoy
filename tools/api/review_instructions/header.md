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

## Priorities

1. Breaking changes as defined by the API versioning policy below. CI does not check wire or
   JSON compatibility, so review must.
2. Whether the change is needed and is placed at the right scope (the Necessity, Consistency
   and Extensibility sections of the review checklist).
3. Field documentation, validation rules and naming per the style guide.

The rest of this file reproduces the API versioning policy, the API style guide and the API
review checklist from `api/`. Relative links in them are relative to `api/`.
