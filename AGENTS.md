# AGENTS.md

Instructions for AI coding agents (Claude Code, Copilot, Cursor, etc.) working in this repository.

## Critical rules

1. **Always sign off commits.** The human user must sign off commits via `git commit -s` — never
   manually write a `Signed-off-by` trailer. The sign-off attests that the committer (the user)
   has the right to submit the code under the project's license.
2. **Always run format and lint checks before committing.** Use `tools/local_fix_format.sh` for
   a quick local check, or run `./ci/do_ci.sh format` inside Docker for the full CI check suite.
   Format failures are the most common CI rejection. If running checks is impractical, warn the
   user that formatting has not been verified.
3. **Never amend commits or force-push after a PR has received human review.** Always create new
   commits to preserve review history.
4. **Never rebase a PR that is under review.** Use `git merge main` instead to pull in recent
   changes. The project squash-merges, so commit count does not matter.
5. **Disclose AI usage.** When submitting PRs, include a note about AI assistance in the PR
   description. The submitter must fully understand all code being submitted.
6. **Never commit to `main`.** Always create a new branch before committing. If switching
   contexts or unsure which branch to use, ask the user.
7. **Always push to a personal fork.** Do not create branches in the main repo.

## Developer workflow

### 1. Before starting work

Read `CONTRIBUTING.md` for the full contribution process. Key points:
- **Major features (>100 LOC or user-facing):** Open a GitHub issue first to discuss design.
  For new extensions, read `EXTENSION_POLICY.md`.
- **Small patches and bug fixes:** No prior communication needed.
- Install git hooks: `./support/bootstrap`

### 2. Writing code

Read `STYLE.md` for the C++ coding style. After writing C++ code, run `clang-format` to fix
formatting automatically rather than trying to hand-format:

```bash
clang-format -i <file>
```

Tests must:
- Live in `test/` mirroring the `source/` structure
- Achieve 100% coverage for new code
- Use `StrictMock` by default, `SimulatedTimeSystem` for time, port 0 for network
- Unit tests must be hermetic and deterministic — no real time, no randomness
- Integration tests (in `test/integration/`) use real network on localhost

### 3. Building and testing

See `bazel/README.md` for full build documentation. Common commands:

```bash
# Docker-based (recommended — matches CI environment)
./ci/run_envoy_docker.sh bash                                   # interactive shell
./ci/do_ci.sh debug //test/common/http/...                      # build + test
./ci/do_ci.sh debug.server_only                                 # build binary only

# Local (requires local dependencies)
bazel test -c dbg //test/common/http/...                         # run tests
bazel build --config=clang -c opt //source/exe:envoy-static      # optimized binary
```

Sanitizers, coverage, GDB debugging, and profiling are resource-intensive. Do **not** run
them unless the user explicitly asks. See `bazel/README.md` and `bazel/PPROF.md`.

### 4. Format and lint checks (required before every commit)

Format failures are the most common CI rejection. Agents should produce content that conforms
to the repo's style conventions for all file types (C++, BUILD, YAML, Markdown, shell, etc.).

**Quick local check (recommended):**

```bash
tools/local_fix_format.sh          # uncommitted changes (default)
tools/local_fix_format.sh -main    # changes since main
tools/local_fix_format.sh -all     # entire repo
```

**Individual checks:**

```bash
bazel run //tools/code_format:check_format -- fix      # C++, BUILD, .bzl, .proto
bazel run //tools/spelling:check_spelling_pedantic -- fix   # spelling
./ci/do_ci.sh format                                   # full CI check (inside Docker)
```

**Linter config files — read these to produce compliant output without running the tools:**

| Config file | What it configures |
|-------------|--------------------|
| `.clang-format` | C++/Proto formatting (100-col, include order, pointer alignment) |
| `.yamllint` | YAML rules (140-col max, consistent indentation) |
| `.flake8` | Python lint rules |
| `rustfmt.toml` | Rust formatting (100-col, 2-space indent) |
| `tools/spelling/spelling_dictionary.txt` | Custom word list (1700+ project terms) |

### 5. Creating the commit

Before your first commit in a session:

1. Check if the local `main` is up to date with `origin/main`:
   ```bash
   git fetch origin
   git log main..origin/main --oneline
   ```
2. If `main` is behind, sync it:
   ```bash
   git checkout main && git pull && git checkout -
   ```
3. Create a new branch off the updated `main`:
   ```bash
   git checkout -b <descriptive-branch-name>
   ```
4. If you had uncommitted changes that conflict with the updated `main`, ask the user whether
   to proceed on the outdated base or resolve conflicts against the new `main`.

If you're switching contexts or unsure which branch to commit to, ask the user before committing.

```bash
git add <files>
git commit -s    # -s adds Signed-off-by automatically; NEVER write it manually
```

### 6. Pushing and creating a PR

**PR title format** — lower-case subsystem prefix followed by a colon:
`docs: fix grammar error`, `router: add x-envoy-overloaded header`

**PR description template** — every PR must fill in:

```
Commit Message: <what this PR does — used as the final squash-merge message>
Additional Description: <context useful to reviewers>
Risk Level: Low | Medium | High
Testing: <what testing was done>
Docs Changes: <description or N/A>
Release Notes: <description or N/A>
```

See `PULL_REQUESTS.md` for full field descriptions and optional fields (runtime guard,
deprecation, platform-specific features).

**Release notes:** User-facing changes **must** add a release note fragment under
`changelogs/current/`. Name the file `<area>__<short-description>.rst`.

### 7. Waiting for CI and review

- Do **not** create draft PRs if you want prompt reviews — draft PRs are not triaged.
- To re-run failed CI tasks, add a `/retest` comment on the PR.
- PRs with no activity for 14+ days may be closed.

### 8. Addressing review comments

- **Never amend or force-push** after a reviewer has looked at the PR. Create new commits.
- **Never rebase.** If you need to incorporate upstream changes:
  ```bash
  git fetch origin main && git merge origin/main
  ```
- If the reviewer asked for a runtime guard, add one (see `CONTRIBUTING.md`).

### 9. After merge

The project squash-merges PRs. The "Commit Message" field in your PR description becomes the
final commit message. Make sure it's up to date before merge.

## Understanding CI

Envoy uses a checks-based CI system. Results appear as **GitHub Check Runs** on PRs, not as
simple workflow pass/fail statuses.

**CI pipeline:**

1. **`Envoy/Prechecks`** — fast checks: format/lint/spelling, dependency validation, docs build
2. **`Envoy/Checks`** — heavier checks: compilation, tests, coverage, sanitizers

**Checking CI status:**

```bash
gh pr checks <PR-number>
gh run view <run-id> --log-failed
```

When CI fails, check the failed check run name to determine which `do_ci.sh` target to
reproduce locally. Format failures come from `Envoy/Prechecks`; build/test failures come
from `Envoy/Checks`.

## Inclusive language

The following terms are **not allowed**:
- ~~whitelist~~ -> allowlist
- ~~blacklist~~ -> denylist / blocklist
- ~~master~~ -> primary / main
- ~~slave~~ -> secondary / replica

## BUILD file conventions

See `bazel/DEVELOPER.md` for full BUILD file rules. Key points:
- Use `envoy_cc_library`, `envoy_cc_test`, `envoy_cc_mock` (not raw `cc_library`)
- Target suffixes: `_lib`, `_test`, `_mocks`, `_interface`
- Every `#include` must have a corresponding `deps` entry

## Updating dependencies

See `bazel/EXTERNAL_DEPS.md` and `DEPENDENCY_POLICY.md`. When updating a version:
1. Update version, sha256, and urls in `bazel/repository_locations.bzl`
2. Update `release_date` in `bazel/deps.yaml` to the UTC date of the new release
3. Prefer maintainer-provided tarballs over GitHub auto-generated ones

## CI and GitHub Actions (for workflow file authors)

- In `if:` conditions, do **not** wrap expressions in `${{ }}` — the `if` field evaluates
  expressions implicitly. Use `${{ }}` only in string contexts (`run:`, `with:`, `env:`).
- Workflow files in `.github/workflows/` are shared across all branches (main and stable release
  branches). Do not remove variables or inputs still referenced by stable branches.

## Key files

| File | Purpose |
|------|---------|
| `STYLE.md` | C++ coding style and error handling |
| `CONTRIBUTING.md` | Contribution guidelines, deprecation, breaking changes |
| `PULL_REQUESTS.md` | PR field descriptions |
| `EXTENSION_POLICY.md` | Extension lifecycle and requirements |
| `DEPENDENCY_POLICY.md` | External dependency rules |
| `RELEASES.md` | Release schedule and backport process |
| `SECURITY.md` | Security reporting and disclosure |
| `REPO_LAYOUT.md` | Repository structure |
| `bazel/README.md` | Building, testing, sanitizers, coverage |
| `bazel/DEVELOPER.md` | BUILD file conventions |
| `bazel/EXTERNAL_DEPS.md` | Managing external dependencies |
| `bazel/PPROF.md` | Performance profiling |
| `source/extensions/extensions_metadata.yaml` | Extension status and security posture |
| `source/common/runtime/runtime_features.cc` | Runtime feature flag defaults |
