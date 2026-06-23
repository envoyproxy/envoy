# AGENTS.md

Instructions for AI coding agents (Claude Code, Copilot, Cursor, etc.) working in this repository.

## Overview

Envoy is a cloud-native high-performance edge/middle/service proxy written in C++20. It uses
Bazel for builds and has a rich extension system. The codebase is large (~2M+ lines) with strict
quality, style, and process requirements.

## Critical rules

1. **Always sign off commits.** The human user must sign off commits via `git commit -s` — never
   manually write a `Signed-off-by` trailer. The sign-off attests that the committer (the user)
   has the right to submit the code under the project's license.
2. **Always run format and lint checks before committing.** Use `tools/local_fix_format.sh` for
   a quick local check, or run `./ci/do_ci.sh format` inside Docker for the full CI check suite.
   CI checks C++, BUILD, YAML, Markdown, shell, Go, Python formatting and spelling. Format
   failures are the most common CI rejection. If running checks is impractical, warn the user
   that formatting has not been verified. Proactively produce content that conforms to the repo's
   style conventions in all file types.
3. **Never amend commits or force-push after a PR has received human review.** Always create new
   commits to preserve review history.
4. **Never rebase a PR that is under review.** Use `git merge main` instead to pull in recent
   changes. The project squash-merges, so commit count does not matter.
5. **Disclose AI usage.** When submitting PRs, include a note about AI assistance in the PR
   description. The submitter must fully understand all code being submitted.
6. **Never commit to `main`.** Always create a new branch before committing. If switching
   contexts or unsure which branch to use, ask the user.
7. **Security issues and crashes must not be opened as regular PRs.** Use
   [GitHub Security Advisories](https://github.com/envoyproxy/envoy/security/advisories/new)
   or email envoy-security@googlegroups.com instead.

## Developer workflow

### 1. Before starting work

Read `CONTRIBUTING.md` for the full contribution process. Key points:
- **Major features (>100 LOC or user-facing):** Open a GitHub issue first to discuss design.
  For new extensions, read `EXTENSION_POLICY.md`.
- **Small patches and bug fixes:** No prior communication needed.
- Install git hooks: `./support/bootstrap` (enables automatic DCO sign-off and format checks).

### 2. Writing code

Follow `STYLE.md` for the full C++ coding style. After writing C++ code, run `clang-format`
to fix formatting automatically rather than trying to hand-format:

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

Builds are typically done inside the CI Docker container:

```bash
# Enter interactive development shell
./ci/run_envoy_docker.sh bash

# Inside the container — build and test
./ci/do_ci.sh debug                                          # full debug build + tests
./ci/do_ci.sh debug //test/common/http/...                   # test a specific directory
./ci/do_ci.sh debug //test/common/http:async_client_impl_test  # single test target
./ci/do_ci.sh debug.server_only                              # build binary only (no tests)
```

Or run one-off commands without entering the shell:

```bash
./ci/run_envoy_docker.sh './ci/do_ci.sh debug //test/common/http/...'
```

Building locally without Docker (requires local dependencies):

```bash
bazel build -c opt //source/exe:envoy-static    # optimized binary
bazel build -c dbg //source/exe:envoy-static    # debug binary
bazel test -c dbg //test/common/http/...         # run tests

# Use hermetic clang toolchain (recommended on Linux)
bazel build --config=clang //source/exe:envoy-static
```

Useful test flags:

```bash
# Verbose test output
bazel test --test_output=streamed //test/common/http:async_client_impl_test

# Pass args to the test binary (e.g. trace logging)
bazel test --test_output=streamed //test/common/http:async_client_impl_test \
  --test_arg="--" --test_arg="-l trace"

# Force re-run (ignore cached results)
bazel test //test/common/http:async_client_impl_test --cache_test_results=no

# IPv4-only or IPv6-only
bazel test //test/... --test_env=ENVOY_IP_TEST_VERSIONS=v4only

# Disable heap checker
bazel test //test/... --test_env=HEAPCHECK=
```

Build output goes to `/tmp/envoy-docker-build/` by default (override with
`ENVOY_DOCKER_BUILD_DIR`).

**Advanced testing (only run when explicitly asked by the user):**

The following operations are resource-intensive. Do **not** run them unless the user specifically
requests it:

```bash
# Sanitizers
bazel test -c dbg --config=asan //test/...           # AddressSanitizer + UBSan
bazel test -c dbg --config=docker-tsan //test/...    # ThreadSanitizer
bazel test -c dbg --config=docker-msan //test/...    # MemorySanitizer

# Coverage
test/run_envoy_bazel_coverage.sh                     # full coverage
VALIDATE_COVERAGE=false test/run_envoy_bazel_coverage.sh //test/common/http/...

# Debugging under GDB
bazel build -c dbg //test/common/http:async_client_impl_test
bazel build -c dbg //test/common/http:async_client_impl_test.dwp
gdb bazel-bin/test/common/http/async_client_impl_test
```

### 4. Format and lint checks (required before every commit)

CI runs multiple checks covering different file types. Format failures are the most common CI
rejection reason — agents should proactively produce content that adheres to the repo's style
conventions for all file types (C++, BUILD, YAML, Markdown, shell scripts, etc.).

**Quick local check (recommended — covers most checks):**

```bash
# Check only changed files (default: uncommitted changes)
tools/local_fix_format.sh

# Check changes since main
tools/local_fix_format.sh -main

# Check entire repo
tools/local_fix_format.sh -all
```

This runs both `check_format` and `check_spelling_pedantic` on the affected files.

**Individual checks (can run inside Docker or locally with Bazel):**

```bash
# C++, BUILD, WORKSPACE, .bzl, .proto, .java formatting (clang-format, buildifier)
bazel run //tools/code_format:check_format -- fix

# Spelling check (.cc, .h, .proto, .js)
bazel run //tools/spelling:check_spelling_pedantic -- fix

# Full CI format check (inside Docker — runs ALL checks below)
./ci/do_ci.sh format
```

**What CI checks (via `ci/format_pre.sh`):**

| Check | Tool | File types |
|-------|------|------------|
| Code formatting | `//tools/code_format:check_format` | C++, BUILD, WORKSPACE, .bzl, .proto, .java |
| Spelling | `//tools/spelling:check_spelling_pedantic` | .cc, .h, .proto, .js |
| YAML lint | `yamllint` (via `//tools/code:check`) | .yaml, .yml |
| Markdown lint | `glint` (via `//tools/code:check`) | .md |
| Shell lint | `shellcheck` (via `//tools/code:check`) | .sh |
| Go format | `gofmt` (via `//tools/code:check`) | .go |
| Python lint | `flake8` (via `//tools/code:check`) | .py |
| Rust format | `@rules_rust//:rustfmt` | .rs |
| Metadata | `//tools/code:check` | Extension metadata, changelogs, runtime guards |

**Linter config files — read these to produce compliant output without running the tools:**

| Config file | What it configures |
|-------------|--------------------|
| `.clang-format` | C++ and Proto formatting (100-col limit, include sort order, pointer alignment) |
| `.clang-tidy` | C++ static analysis checks (naming conventions, modernize, performance, abseil) |
| `.yamllint` | YAML rules (140-col max, consistent indentation, truthy values) |
| `.flake8` | Python lint rules (ignored warnings, excluded paths) |
| `rustfmt.toml` | Rust formatting (100-col max, 2-space tabs, import grouping) |
| `tools/code_format/config.yaml` | Format checker rules (allowed exceptions, path exclusions, code conventions) |
| `tools/spelling/spelling_dictionary.txt` | Custom word list for the spelling checker (1700+ project-specific terms) |

Agents should read the relevant config files before writing or modifying code to avoid
formatting violations. Running the full `./ci/do_ci.sh format` inside Docker is the most
reliable way to match CI. If running these checks is expensive or impractical, ask the user
whether to run them before committing — but always strive to write compliant content in the
first place.

### 5. Creating the commit

**Never commit to `main`.** Before your first commit in a session:

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

The DCO (Developer Certificate of Origin) sign-off is **required** for all commits.

### 6. Pushing and creating a PR

**PR title format** — lower-case subsystem prefix followed by a colon:
- `docs: fix grammar error`
- `http conn man: add new feature`
- `router: add x-envoy-overloaded header`
- `tls: add support for specifying TLS session ticket keys`

**PR description template** — every PR must fill in:

```
Commit Message: <what this PR does — used as the final squash-merge message>
Additional Description: <context useful to reviewers>
Risk Level: Low | Medium | High
Testing: <what testing was done>
Docs Changes: <description or N/A>
Release Notes: <description or N/A>
Platform Specific Features: <if applicable>
[Optional Runtime guard:]
[Optional Fixes #Issue]
[Optional Deprecated:]
```

**Risk levels:**
- **Low:** Small bug fix or small optional feature
- **Medium:** New features not yet enabled, small-medium additions to existing components
- **High:** Flow control changes, rewrites of critical components

**Release notes:** Any user-facing or extension-developer-facing change **must** add a release
note fragment under `changelogs/current/`. Name the file `<area>__<short-description>.rst`.
Use sections defined in `changelogs/changelogs.yaml`.

**Deprecations:** If deprecating user-facing configuration, add a fragment under the `deprecated`
section in `changelogs/current/` and mention it in the PR description.

**Always** push to a personal fork. Do not create branches in the main repo.

### 7. Waiting for CI and review

- Tests run automatically on PRs. No PR will be merged with failing tests.
- Do **not** create draft PRs if you want prompt reviews — draft PRs are not triaged.
- To re-run failed CI tasks, add a `/retest` comment on the PR.
- To re-run all CI (e.g. stuck tasks): `git commit -s --allow-empty -m 'Kick CI' && git push`
- Maintainers typically review within one business day.
- PRs with no activity for 14+ days may be closed.

### 8. Addressing review comments

- **Never amend or force-push** after a reviewer has looked at the PR. Create new commits.
- **Never rebase.** If you need to incorporate upstream changes:
  ```bash
  git fetch origin main && git merge origin/main
  ```
- Update the PR title and commit message if the PR changed significantly during review.
- If the reviewer asked for a runtime guard, add one (see "Runtime guarding" below).

### 9. After merge

The project squash-merges PRs. The "Commit Message" field in your PR description becomes the
final commit message. Make sure it's up to date before merge.

## C++ coding style

Read `STYLE.md` for the full C++ coding style. Envoy follows the
[Google C++ style guide](https://google.github.io/styleguide/cppguide.html) with project-specific
deviations documented there. Run `clang-format` on all C++ files after editing — do not
hand-format.

### Inclusive language

The following terms are **not allowed**:
- ~~whitelist~~ -> allowlist
- ~~blacklist~~ -> denylist / blocklist
- ~~master~~ -> primary / main
- ~~slave~~ -> secondary / replica

## BUILD files and Bazel rules

Envoy uses custom Bazel rules defined in `bazel/envoy_build_system.bzl`.

**Naming conventions for targets:**
- `_interface` suffix for pure virtual interface headers: `bar_interface`
- `_lib` suffix for implementation libraries: `bar_lib`
- `_mocks` suffix for mock targets: `foo_mocks`
- `_test` suffix for test targets: `bar_impl_test`

**Common rules:**
- `envoy_cc_library` — library targets (use `alwayslink = 1` for filter registration)
- `envoy_cc_test` — test targets
- `envoy_cc_mock` — mock targets
- `envoy_cc_binary` — binary targets (tools, etc.)
- `envoy_cc_extension` — extension targets (with security posture tag)

**Key principles:**
- Every `#include` must have a corresponding `deps` entry in the BUILD file
- Targets should be sorted alphabetically by `name` in BUILD files
- Use [buildifier](https://github.com/bazelbuild/buildtools/tree/master/buildifier) for formatting

**Adding an external dependency:**
1. For native Bazel deps: add repository in `bazel/repositories.bzl`, reference via `deps`
2. For CMake deps: add source repo in `bazel/repositories.bzl`, add `envoy_cmake` rule in
   `bazel/foreign_cc/BUILD`
3. For Python deps: add to `requirements.txt` via `pip_install` in `bazel/repositories_extra.bzl`

**Updating a dependency version:**
1. Update the version, sha256, and urls in `bazel/repository_locations.bzl`
2. Update the `release_date` in `bazel/deps.yaml` to the UTC date of the new release
3. Prefer release tarballs provided by maintainers over GitHub auto-generated tarballs
   (GitHub-generated tarball SHA256 values can change when GitHub updates their tar/gzip libraries)
4. Use `{version}` interpolation in `strip_prefix` and `urls`
5. Run tests to verify

**Overriding a dependency locally (for debugging):**
```bash
bazel build --override_repository=dep_name=/absolute/path/to/local/copy //source/exe:envoy-static
```

## Runtime guarding

High-risk behavioral changes and user-visible non-config-guarded changes to protocol processing
should be runtime guarded:

```cpp
if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.my_feature_name")) {
  // new code path
} else {
  // old code path
}
```

Runtime features are enabled by default in `source/common/runtime/runtime_features.cc`.
Both code paths must have 100% test coverage. Release notes must document how to revert.

## Deprecation process

Deprecated features must log a warning. Add deprecation fragments under `changelogs/current/`
in the `deprecated` section. Changing the default behavior of `ext_authz` and `ext_proc` is
strictly forbidden. See `CONTRIBUTING.md` for the full deprecation and breaking change policy.

## Extension policy

### Adding new extensions

1. Open a GitHub issue describing the proposed extension
2. Get sponsorship from an existing maintainer
3. Propose two reviewers (codified in CODEOWNERS)
4. New dependencies must comply with `DEPENDENCY_POLICY.md`
5. Extension PRs must not modify core Envoy code — core changes go in separate PRs
6. Tag with `status` (`stable`, `alpha`, `wip`) and `security_posture` in
   `source/extensions/extensions_metadata.yaml`

### Contrib extensions

- Lower barrier to entry, but not included in main image builds
- Require an end-user sponsor
- Not eligible for security team coverage
- API should use `v3alpha`
- Config in `api/contrib/envoy/`, build config in `contrib/contrib_build_config.bzl`

## Understanding CI

Envoy uses a checks-based CI system. Results appear as **GitHub Check Runs** on PRs, not as
simple workflow pass/fail statuses.

**CI pipeline stages:**

1. **`Request`** — triggers on PR events, loads configuration
2. **`Envoy/Prechecks`** — fast checks that run first:
   - `format` — code formatting, linting, spelling (runs `ci/format_pre.sh`)
   - `deps` — dependency validation
   - `publish` — docs build
   - `external` — external dependency checks
3. **`Envoy/Checks`** — heavier checks that run after prechecks:
   - `build` — compilation and unit/integration tests
   - `coverage` — code coverage analysis
   - `san` — sanitizer builds (ASAN, TSAN, MSAN)
   - `runtime` — runtime guard validation

**Checking CI status on a PR:**

```bash
# View all check runs for a PR
gh pr checks <PR-number>

# View specific workflow run details
gh run view <run-id> --log-failed
```

**Recreating CI locally (Docker-based):**

The CI runs inside a Docker container. To reproduce the CI environment locally:

```bash
# Enter the same Docker environment CI uses
./ci/run_envoy_docker.sh bash

# Inside the container, run specific CI targets:
./ci/do_ci.sh format                              # format/lint checks (prechecks)
./ci/do_ci.sh debug //test/common/http/...        # build + test (checks)
./ci/do_ci.sh asan //test/common/http/...         # sanitizer build (checks)
./ci/do_ci.sh coverage //test/common/http/...     # coverage (checks)
```

When CI fails, check the failed check run name to determine which `do_ci.sh` target to
reproduce locally. Format failures come from `Envoy/Prechecks`; build/test failures come
from `Envoy/Checks`.

## CI and GitHub Actions (for workflow file authors)

- In `if:` conditions, do **not** wrap expressions in `${{ }}` — the `if` field evaluates
  expressions implicitly. Use `${{ }}` only in string contexts (`run:`, `with:`, `env:`).
- Workflow files in `.github/workflows/` are shared across all branches (main and stable release
  branches). Do not remove environment variables or inputs from workflow files if they are still
  referenced by stable branches. Split changes: merge script changes first, backport to all
  stable branches, then clean up workflow files in a follow-up PR.

## Dependencies

External dependencies are declared in `bazel/repository_locations.bzl` or
`api/bazel/repository_locations.bzl`.

Key rules:
- Prefer release versions over SHA tarballs; prefer maintainer-provided tarballs over GitHub
  auto-generated ones
- Must have a CNCF-approved license
- Must include a CPE for non-build/test dependencies
- Must not substantially increase binary size (Envoy Mobile is size-sensitive)
- No duplication of existing dependencies
- Must be hosted on a git repository (no intermediate artifacts on GCS/S3)
- New core dependencies require sign-off from dependency shepherds and the security team
- Dependency patches (`.patch` files) require an upstream PR effort and a tracking issue
- When updating a dependency, update both `bazel/repository_locations.bzl` (version, sha256, urls)
  and `bazel/deps.yaml` (release_date)
- Python dependencies: pin to exact versions with SHA256 checksums

## Releases and backports

- Major releases happen quarterly (15th day of each quarter, ±2 weeks)
- Security releases happen on a 3-monthly cycle between major releases
- Extended maintenance window: any version released in the last 12 months
- Only security and reliability fixes qualify for backporting
- To nominate a backport: add `backport/review` label or use repokitteh's `/backport` command
- When raising a backport PR, raise against **all** affected supported branches
- Use cherry-pick (not merge) for backport PRs; manage with rebase, not merge

## IDE setup

Generate a compilation database for clangd / clang-tidy / YouCompleteMe (only when asked):

```bash
TEST_TMPDIR=/tmp tools/gen_compilation_database.py
```

## Performance profiling

Do **not** run profiling unless the user explicitly asks. See `bazel/PPROF.md` for instructions
on CPU/heap profiling with gperftools and Perfetto tracing.

## Repository structure

| Directory | Purpose |
|-----------|---------|
| `source/common/` | Core code shared across Envoy (not server-specific) |
| `source/server/` | Standalone server implementation |
| `source/exe/` | Final production binary |
| `source/extensions/` | All extensions (filters, transport sockets, tracers, etc.) |
| `envoy/` | Public interface headers (mostly abstract classes) |
| `api/` | Envoy data plane API definitions (protobuf) |
| `test/` | All tests — mirrors `source/` structure |
| `test/integration/` | End-to-end integration tests |
| `test/mocks/` | Mock implementations of core interfaces |
| `contrib/` | Contrib extensions (non-core, lower barrier to entry) |
| `bazel/` | Bazel build configuration and docs |
| `ci/` | CI scripts and Docker build scripts |
| `changelogs/current/` | Release note fragments for the current version |
| `docs/` | User-facing documentation |
| `tools/` | Development tools (format checkers, code generators, etc.) |
| `support/` | Git hooks and development support scripts |

### Extension layout

Extensions live under `source/extensions/` organized by type. Each extension is in its own
namespace under `Envoy::Extensions::`. Key extension types:

- `filters/http/` — HTTP L7 filters (`Envoy::Extensions::HttpFilters`)
- `filters/network/` — L4 network filters (`Envoy::Extensions::NetworkFilters`)
- `filters/listener/` — Listener filters (`Envoy::Extensions::ListenerFilters`)
- `transport_sockets/` — Transport socket implementations
- `tracers/` — Distributed tracing
- `clusters/` — Cluster extensions
- `access_loggers/` — Access log implementations

Extensions are registered in `source/extensions/all_extensions.bzl` (cannot be removed) or
`source/extensions/extensions_build_config.bzl` (optional). Common code shared across extensions
goes in `common/` directories close to the extensions that use it.

## Quick reference: key files

| File | Purpose |
|------|---------|
| `STYLE.md` | Detailed C++ coding style and error handling |
| `CONTRIBUTING.md` | Full contribution guidelines and policies |
| `PULL_REQUESTS.md` | PR field descriptions |
| `EXTENSION_POLICY.md` | Extension lifecycle and requirements |
| `DEPENDENCY_POLICY.md` | External dependency rules |
| `RELEASES.md` | Release schedule and process |
| `SECURITY.md` | Security reporting and disclosure process |
| `REPO_LAYOUT.md` | Detailed repository structure |
| `CODEOWNERS` | Extension and component ownership |
| `bazel/README.md` | Building and testing with Bazel |
| `bazel/DEVELOPER.md` | Writing Envoy Bazel BUILD rules |
| `bazel/EXTERNAL_DEPS.md` | Managing external dependencies |
| `bazel/PPROF.md` | Performance profiling with gperftools |
| `source/extensions/extensions_metadata.yaml` | Extension status and security posture |
| `bazel/repository_locations.bzl` | External dependency declarations |
| `changelogs/current/` | Release note fragments for the next release |
| `source/common/runtime/runtime_features.cc` | Runtime feature flag defaults |
