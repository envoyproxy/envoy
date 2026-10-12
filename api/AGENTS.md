# Guide for AI Agents: the xDS API (`api/`)

Instructions for AI agents changing the xDS API protos under `api/`. These protos are consumed by
many control planes and by xDS clients other than Envoy (e.g. gRPC), and the v3 API is never
broken, so mistakes here are permanent.

## Read first

- [STYLE.md](STYLE.md): conventions, annotations, and how to document fields.
- [review_checklist.md](review_checklist.md): the questions API shepherds ask. Answer the relevant
  ones in the PR description.
- [API_VERSIONING.md](API_VERSIONING.md): what counts as a breaking change.

## Rules that are most often missed

- Never change the number, type, name, JSON name, cardinality or `oneof` membership of an existing
  field, and never tighten its `validate.rules`. CI does not reliably catch these.
- An unset new field must preserve existing behavior. Behavior changes need a runtime guard
  (`source/common/runtime/runtime_features.cc`) and a release note.
- Before adding a field, check whether the behavior should simply become the default, or whether an
  existing generic mechanism or extension point already covers it.
- Do not add `oneof` to new fields. When a new field supersedes an old one, the new one takes
  precedence and setting both must not be rejected.
- Use `google.protobuf.Duration` for time, plural names for `repeated` fields, and `validate.rules`
  bounds on every number, duration, string and repeated field.
- Document each new field completely: default when unset, meaning of zero or empty values, units,
  format, failure behavior and interactions with other fields. Write the comment in RST and refer
  to "the client" rather than "Envoy" where behavior is not Envoy specific.
- Put protocol specific settings in protocol options and extension specific settings in the
  extension's config, not on `Cluster` or `HttpConnectionManager`.

## Workflow

1. Edit the `.proto` files. For a new extension, follow
   [Adding an extension configuration to the API](STYLE.md#adding-an-extension-configuration-to-the-api).
2. Commit, then run `./tools/proto_format/proto_format.sh fix` to regenerate `BUILD` files and
   formatting. The tool compares against the committed state, so commit before running it.
3. Build the docs (`./ci/do_ci.sh docs`) when comments change, since RST errors only show up there.

## Reviewer guidance

Pull requests that touch `api/` get an automated Copilot review driven by
`.github/instructions/api-review.instructions.md`. That file is a condensed, self-contained copy of
the rules in STYLE.md and review_checklist.md (Copilot cannot follow links). Keep the three in sync
when changing API guidance.
