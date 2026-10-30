# AGENTS.md

Instructions for AI coding agents (Claude Code, Copilot, Cursor, etc.) working in this repository.

Some subdirectories have their own, more specific guides. Read them before working in (or
debugging failures that involve) those areas:

| Guide | Covers |
|-------|--------|
| `compat/openssl/AGENTS.md` | The OpenSSL compatibility layer (`bssl-compat`) and `--config=openssl` build failures |
| `.github/copilot-instructions.md` | GitHub Copilot cloud agent environment setup |

## Critical rules

1. **Always sign off commits.** The human user must sign off commits via `git commit -s` — never
   manually write a `Signed-off-by` trailer. The sign-off attests that the committer (the user)
   has the right to submit the code under the project's license.
2. **Always run format and lint checks before committing.** Use `tools/local_fix_format.sh` for
   a quick local check, or run `./ci/do_ci.sh format` inside Docker for the full CI check suite.
   Format failures are the most common CI rejection. If running checks is impractical, warn the
   user that formatting has not been verified.
3. **Never amend commits, force-push or rebase after a PR has received human review.** Always
   create new commits to preserve review history, and use `git fetch origin main && git merge
   origin/main` to pull in recent changes. The project squash-merges, so commit count does not
   matter.
4. **Follow the generative AI policy.** See the "Use of generative AI policy" section of
   `CONTRIBUTING.md`. Disclose AI assistance in the PR description; the submitter must fully
   understand all code being submitted.
5. **Never commit to `main`.** Always create a new branch before committing. If switching
   contexts or unsure which branch to use, ask the user.
6. **Always push to a personal fork.** Do not create branches in the main repo.

## Developer workflow

### 1. Before starting work

Read `CONTRIBUTING.md` for the full contribution process. Key points:
- **Major features (>100 LOC or user-facing):** Open a GitHub issue first to discuss design.
  For new extensions, read `EXTENSION_POLICY.md`.
- **Small patches and bug fixes:** No prior communication needed.
- Install git hooks: `./support/bootstrap`

### 2. Writing code

Read `STYLE.md` for the C++ coding style. After writing code, use the format tooling (see
[Format and lint checks](#4-format-and-lint-checks-required-before-every-commit)) rather than
trying to hand-format. Prefer the Bazel targets: they use the pinned `//tools/clang-format`, and a
locally installed `clang-format` may produce different output from CI.

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
bazel build -c opt //source/exe:envoy-static                    # optimized binary
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

The script runs the Bazel targets below. With `-skip-bazel` it instead requires `clang-format`,
`buildifier` and `buildozer` on `PATH`, which may not match the CI versions.

**Individual checks:**

```bash
bazel run //tools/code_format:check_format -- fix      # C++, BUILD, .bzl, .proto
bazel run //tools/spelling:check_spelling_pedantic -- fix   # spelling
./tools/proto_format/proto_format.sh fix               # API protos under api/
./ci/do_ci.sh format                                   # full CI check (inside Docker)
```

**Linter configs** (follow them to produce compliant output without running the tools):
`.clang-format` (C++/proto, 100 cols), `.yamllint` (140 cols), `.flake8`, `rustfmt.toml`, and
`tools/spelling/spelling_dictionary.txt` (project word list).

### 5. Creating the commit

Before your first commit in a session, `git fetch origin` and make sure the new branch is based
on an up-to-date `main` (`git checkout -b <descriptive-branch-name> origin/main`). If
uncommitted changes conflict with the new `main`, or you are unsure which branch to use, ask the
user.

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

**Release notes:** User-facing changes **must** add a release note fragment at
`changelogs/current/<section>/<area>__<short-description>.rst`, e.g.
`changelogs/current/bug_fixes/http__fix-header-crash.rst`. Both `<section>` (e.g. `bug_fixes`,
`new_features`, `behavior_changes`) and `<area>` must be listed in `changelogs/changelogs.yaml`;
a `/` in an area name is written as `~` in the filename. If a change fits several sections, use
only the most important one.

### 7. Waiting for CI and review

- Do **not** create draft PRs if you want prompt reviews — draft PRs are not triaged.
- To re-run failed CI tasks, add a `/retest` comment on the PR.
- PRs with no activity for 14+ days may be closed.

### 8. Addressing review comments

- Push new commits; never amend, force-push or rebase (see critical rule 3).
- If the reviewer asked for a runtime guard, add one (see [Runtime guards](#runtime-guards)).

### 9. After merge

The project squash-merges PRs. The "Commit Message" field in your PR description becomes the
final commit message. Make sure it's up to date before merge.

## Understanding CI

CI results are **GitHub Check Runs**: `Envoy/Prechecks` (format, deps, docs, publish) and
`Envoy/Checks` (builds, tests, coverage, sanitizers). Use `gh pr checks <PR>` and
`gh run view <run-id> --log-failed`.

Each CI job runs `ci/do_ci.sh <target>` (see `ci/README.md`), usually adding a `.bazelrc`
`--config`. Reproduce failures with the same target (ideally inside `./ci/run_envoy_docker.sh`),
or with `bazel` and the same `--config` on the failing targets:

| CI job | `do_ci.sh` target | Bazel config / notes |
|--------|-------------------|----------------------|
| Prechecks: format | `format`, `format-api` | |
| Prechecks: deps | `deps` | |
| Prechecks: publish | `release.test_only`, `config` | |
| Prechecks: external | `external` | downstream bzlmod build in `bazel/tests/external` |
| Checks: build | `api`, `compile_time_options`, `execution_context`, `gcc` | `--config=gcc`, `--config=compile-time-options` |
| Checks: openssl | `openssl.presubmit` (PRs), `openssl` (post-submit) | `--config=openssl`; allowed to fail |
| Checks: coverage | `coverage`, `fuzz_coverage` | `--config=coverage`; limits in `test/coverage.yaml` |
| Checks: runtime | `cpu-detection` | |
| Checks: san | `asan`, `msan`, `tsan` | `--config=asan`/`msan`/`tsan`; only run when asked |

### SSL library variants (BoringSSL, BoringSSL-FIPS, OpenSSL)

See `bazel/SSL.md`. Use the `--config` options; never set `//bazel:ssl`, `//bazel:crypto` or
`//bazel:fips` directly. `--config=openssl` builds Envoy against BoringSSL headers backed by the
`compat/openssl/` layer, defines `ENVOY_SSL_OPENSSL` (BUILD files: `//bazel:using_openssl`),
disables HTTP/3 and skips tests tagged `nofips`/`quiche`. **For any OpenSSL build failure, read
`compat/openssl/AGENTS.md` first.**

## Inclusive language

Not allowed: whitelist (use allowlist), blacklist (denylist/blocklist), master (primary/main),
slave (secondary/replica).

## BUILD file conventions

See `bazel/DEVELOPER.md` for full BUILD file rules. Key points:
- Use `envoy_cc_library`, `envoy_cc_test`, `envoy_cc_mock` (not raw `cc_library`)
- Target suffixes: `_lib`, `_test`, `_mocks`, `_interface`
- Every `#include` must have a corresponding `deps` entry

## Runtime guards

High-risk changes and most user-visible protocol-processing changes should be runtime guarded.
See the "Runtime guarding" section of `CONTRIBUTING.md`. In short:
- Guard with `Runtime::runtimeFeatureEnabled("envoy.reloadable_features.<name>")`, keeping both
  the old and new code paths; `reloadable_features` must be safe to flip on a running Envoy.
- Features default to true by being listed in `source/common/runtime/runtime_features.cc`.
- Test both code paths, and describe the flag (and how to revert) in the release note.
- Removing a flag and its legacy code path needs a fragment in the
  `removed_config_or_runtime` changelog section.

## API changes and Mobile

- Protos under `api/` follow `api/CONTRIBUTING.md`; format them with
  `./tools/proto_format/proto_format.sh fix` (CI: `./ci/do_ci.sh format-api`).
- `mobile/` is a separate Bazel module (own `MODULE.bazel`, `.bazelrc`, lockfile); run its Bazel
  commands from inside `mobile/`.

## New extensions

Read `EXTENSION_POLICY.md` first; new extensions need a sponsoring maintainer. A new extension
typically also needs:
- An entry in `source/extensions/extensions_build_config.bzl` (or
  `contrib/contrib_build_config.bzl` for contrib extensions)
- An entry in `source/extensions/extensions_metadata.yaml` (status, security posture,
  categories)
- `CODEOWNERS` entries listing the two proposed extension reviewers
- Docs and a `new_features` release note

## Updating dependencies

See `bazel/EXTERNAL_DEPS.md` and `DEPENDENCY_POLICY.md`. Dependencies are managed with bzlmod and
the [Envoy Bazel registry](https://github.com/envoyproxy/bazel-registry); patches live in the
registry, not in this repo. When updating a version:
1. Update the module in the Envoy Bazel registry, then the `bazel_dep` version in `MODULE.bazel`
   and the corresponding `deps.yaml` entry (`bazel/deps.yaml` or `api/bazel/deps.yaml`)
2. Update `release_date` in `deps.yaml` to the UTC date of the new release
3. Re-pin the registry and regenerate lockfiles with `./ci/do_ci.sh registry` (see
   `ci/README.md`)
4. Some dependencies are also pinned in `mobile/MODULE.bazel` or `api/MODULE.bazel`; update all
   of them together
5. Prefer maintainer-provided tarballs over GitHub auto-generated ones

## CI and GitHub Actions (for workflow file authors)

- In `if:` conditions, do **not** wrap expressions in `${{ }}` — the `if` field evaluates
  expressions implicitly. Use `${{ }}` only in string contexts (`run:`, `with:`, `env:`).
- Workflow files in `.github/workflows/` are shared across all branches (main and stable release
  branches). Do not remove variables or inputs still referenced by stable branches.

## Key files

`STYLE.md` (C++ style, error handling), `CONTRIBUTING.md` (process, AI policy, runtime guards,
deprecation), `PULL_REQUESTS.md`, `EXTENSION_POLICY.md`, `DEPENDENCY_POLICY.md`, `RELEASES.md`
(schedule, backports), `SECURITY.md`, `REPO_LAYOUT.md`, `bazel/README.md` (builds, sanitizers,
coverage), `bazel/DEVELOPER.md` (BUILD rules), `bazel/EXTERNAL_DEPS.md`, `bazel/SSL.md`,
`bazel/PPROF.md`, `ci/README.md` (`do_ci.sh` targets).
