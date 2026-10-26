# Envoy Github workflows

## Trusted workflows

Github workflows that are **not** triggered by a `pull_request` generally run with
the repository context/permissions.

In various ways, these workflows can be triggered as the result of a `pull_request`
and/or be made to run untrusted code (ie PR code).

This can be useful, but carries significant risks.

In particular this can effect:

- `pull_request_target`
- `workflow_run`
- `workflow_dispatch`

Do not use these trigger events unless they are required.

## Restrict global permissions and secrets in trusted workflows

If a job requires specific permissions, these should be added on per-job basis.

Global permissions should be set as follows:

```yaml
permissions:
  contents: read
```

Likewise, any secrets that a job requires should be set per-job.

## Restrict access to `workflow_dispatch`

It is important to restrict who can trigger these types of workflow.

Do not allow any bots or app users to do so, unless this is specifically required.

For example, you could add a `job` condition to prevent any bots from triggering the workflow:

```yaml
    if: >-
      ${{
          github.repository == 'envoyproxy/envoy'
          && (github.event.schedule
              || !contains(github.actor, '[bot]'))
      }}
```

## Trusted/untrusted CI jobs

If a trusted workflow is used to run untrusted code, then the entire job that runs this code
should be treated as untrusted.

In this case, it is **essential** to ensure:

- no write permissions in the untrusted job
- no secrets in the untrusted job

## The `trusted` flag

CI derives a `trusted` flag from the triggering event and actor when a request is created,
and every downstream workflow re-reads it (via the `env` artifact, see below). Anything
gated on `trusted` - secrets selection, publishing, cache writes - must handle it correctly:

- Job outputs are **strings**: the string `"false"` is truthy in workflow expressions.
  Never use `needs.<job>.outputs.trusted` bare - always normalize with `fromJSON()`,
  guarding against the empty string where the value may be unset:

  ```yaml
  trusted: ${{ needs.load.outputs.trusted && fromJSON(needs.load.outputs.trusted) || false }}
  ```

- Workflow inputs declared `type: boolean` (eg `inputs.trusted` in reusable workflows) are
  real booleans and safe to use directly.

## Artifact trust boundary

**Any artifact produced by a job that ran untrusted code is attacker-controlled.**

Artifacts are the only channel crossing from untrusted jobs into credentialed ones (the
`env` artifact carrying `trusted` and the request data, built docs/binaries/images,
metadata files). The integrity of that channel *is* the trust boundary of the CI system,
and the consumer is responsible for policing it:

- Consuming workflows triggered by `workflow_run` must gate every job that can see
  secrets on:
  - the triggering run's repository:
    `github.event.workflow_run.repository.full_name == github.repository`
  - an allow-list of triggering events (`github.event.workflow_run.event`), which must
    never include events (eg `pull_request`) whose workflows can be defined in a fork

  either directly in the job's own `if:`, or inherited by needing a gated job. Note that
  `always()`/`!cancelled()` conditions void `needs`-inheritance - such jobs must carry the
  guards themselves.
- Data read from an artifact must never select *where* privileged operations write or
  *what* they trust: destinations (buckets, registries, repos) must be derived from
  workflow `vars`/context based on `trusted`, and any artifact-supplied parameters
  (shas, paths, redirects) must be validated against a strict schema/allow-list before
  use (see `_upload_gcs.yml` for the pattern).
- Never look up runs/artifacts by attacker-influenceable keys (eg `head_sha`) - resolve
  artifacts from the triggering `workflow_run.id` only.
- Artifact-derived values must not be interpolated into `run:` scripts or `jq` filters
  with `${{ }}` - pass them via `env:` (shell) or `--arg` (jq).
