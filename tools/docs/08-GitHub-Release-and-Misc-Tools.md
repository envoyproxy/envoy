# GitHub, Release, and Miscellaneous Tools

This document covers the remaining tools: GitHub operations, deprecation management, release/distribution utilities, code coverage, and other specialized utilities.

---

## Overview

```
tools/
├── github/                            # GitHub source version and reviewer sync
│   ├── write_current_source_version.py
│   └── sync_assignable.py
├── repo/                              # Notification tools
│   ├── notify.py                      # Reviewer notifications (Slack, GitHub)
│   └── security_notify.py            # Security-specific notifications
├── git/                               # Git helper scripts
│   ├── last_github_commit.sh
│   └── modified_since_last_github_commit.sh
├── deprecate_features/                # Proto field deprecation automation
│   └── deprecate_features.py
├── deprecate_guards/                  # Deprecation guard lifecycle management
│   └── deprecate_guards.py
├── distribution/                      # Docker Hub management
│   └── update_dockerhub_repository.py
├── coverage/                          # Code coverage reporting
│   ├── report_generator.sh.template
│   ├── validate.sh
│   ├── filter.jq
│   └── templates/
├── h3_request/                        # HTTP/3 request tool
│   └── h3_request.py
├── sha/                               # SHA string replacement
│   └── replace.sh
└── socket_passing/                    # Hot restart test helper
    └── socket_passing.py
```

---

## GitHub Tools

### `tools/github/write_current_source_version.py`

Writes the `SOURCE_VERSION` file from `VERSION.txt` combined with the current Git commit hash. This version string is embedded in the Envoy binary and reported via the admin interface and server headers.

**Usage:**

```bash
bazel run //tools/github:write_current_source_version
```

### `tools/github/sync_assignable.py`

Syncs the list of assignable reviewers for the repository. Ensures the GitHub reviewer assignment system has an up-to-date list of maintainers and area owners.

---

## Notification Tools

### `tools/repo/notify.py`

Sends notifications to reviewers about PRs, issues, and other repository events. Supports multiple channels:
- **GitHub** — comments, review requests
- **Slack** — channel and DM notifications

### `tools/repo/security_notify.py`

A specialized notifier for security-related events. Sends alerts through security-specific channels when:
- Security advisories are published
- CVEs affecting dependencies are discovered
- Security-sensitive PRs are opened

---

## Git Helper Scripts

### `tools/git/last_github_commit.sh`

Returns the SHA of the last commit that was pushed to GitHub. Useful in CI to determine the baseline for incremental checks.

### `tools/git/modified_since_last_github_commit.sh`

Lists files modified since the last GitHub commit. Used by CI to scope checks to only changed files, dramatically reducing check time on large PRs.

---

## Deprecation Tools

Envoy has a formal deprecation process for API fields and runtime features. These tools automate parts of that lifecycle.

### `tools/deprecate_features/deprecate_features.py`

Automates the deprecation of proto fields:

1. Scans API proto files for fields marked with deprecation annotations
2. Identifies fields that have passed their deprecation timeline
3. Updates `runtime_features.cc` to flip the corresponding runtime guards
4. Generates the necessary code changes for the deprecation

**Usage:**

```bash
bazel run //tools/deprecate_features:deprecate_features
```

### `tools/deprecate_guards/deprecate_guards.py`

Manages the full lifecycle of deprecation guards through the GitHub API:

1. Creates GitHub issues tracking upcoming deprecations
2. Opens PRs to remove deprecated features after the grace period
3. Tracks the status of deprecation timelines

**Requires:** `GITHUB_TOKEN` environment variable with appropriate repository permissions.

**Usage:**

```bash
GITHUB_TOKEN=ghp_... bazel run //tools/deprecate_guards:deprecate_guards
```

---

## Distribution Tools

### `tools/distribution/update_dockerhub_repository.py`

Updates Docker Hub repository metadata for official Envoy images:
- Repository description
- Full description (README)
- Links and tags

Used during the release process to keep Docker Hub pages current with each Envoy release.

---

## Code Coverage

### `tools/coverage/`

Generates code coverage reports from test runs.

**Components:**

- **`report_generator.sh.template`** — Jinja2 template for the shell script that processes `lcov` data and generates HTML reports
- **`validate.sh`** — Validates that coverage meets minimum thresholds; fails CI if coverage drops below the configured level
- **`filter.jq`** — JQ filter for processing coverage JSON data
- **`templates/`** — HTML templates for the coverage report:
  - `base.html` — Page layout
  - `index.html` — Coverage summary dashboard
  - `macros.html` — Reusable Jinja2 macros for rendering coverage tables and charts

---

## HTTP/3 Request Tool

### `tools/h3_request/h3_request.py`

A standalone HTTP/3 (QUIC) request tool built with the `aioquic` library. Useful for testing Envoy's QUIC/HTTP3 support without needing a full browser or curl with HTTP/3 support compiled in.

**Usage:**

```bash
bazel run //tools/h3_request:h3_request -- https://localhost:10000/path
```

---

## SHA Replacement Tool

### `tools/sha/replace.sh`

Bulk-replaces SHA strings across the repository. Used when updating dependency references that are pinned by SHA hash.

**Usage:**

```bash
# Format: target_sha:replacement_sha
tools/sha/replace.sh "abc123:def456"
```

Searches all relevant files (`.bzl`, `BUILD`, etc.) and performs the replacement.

---

## Socket Passing Tool

### `tools/socket_passing/socket_passing.py`

A helper for Envoy's hot restart integration tests. It:

1. Queries the Envoy admin endpoint for current listener addresses
2. Updates the test configuration with the actual bound addresses
3. Enables socket passing between the old and new Envoy processes during hot restart

This solves the problem of dynamic port allocation in tests — when Envoy binds to port 0, the actual port must be communicated to the replacement process.

**Usage:**

```bash
bazel run //tools/socket_passing:socket_passing
```

---

## Navigation

| Previous | Up | Next |
|----------|------|------|
| [07-IDE-and-Editor-Tools.md](07-IDE-and-Editor-Tools.md) | [01-Overview.md](01-Overview.md) | [09-Adding-New-Tools.md](09-Adding-New-Tools.md) |
