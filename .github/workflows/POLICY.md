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
