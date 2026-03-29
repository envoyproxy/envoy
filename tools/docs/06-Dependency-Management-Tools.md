# Dependency Management Tools

This document covers the tools that validate, track, and secure Envoy's external dependencies.

---

## Overview

Envoy has a large number of external dependencies managed through Bazel. These tools ensure dependencies are properly pinned, free of known vulnerabilities, and conform to the project's security and metadata standards.

```
tools/
├── dependency/                        # Dependency validation and CVE tracking
│   ├── validate.py                    # Validate deps against repository_locations.bzl
│   ├── validate_test.py              # Tests for validator
│   ├── cve_fetch.py                   # Fetch CVE data for dependencies
│   ├── cve_update.sh                  # Update CVE database
│   ├── cves.sh                        # CVE summary report
│   ├── cve.yaml                       # CVE configuration
│   ├── cve_matcher.jq                 # JQ filter for CVE matching
│   ├── cve_report.jq                  # JQ filter for CVE reporting
│   ├── cve_utils.jq                   # JQ utility functions for CVEs
│   ├── ossf_scorecard.py              # OSSF Scorecard integration
│   ├── version.sh                     # Version extraction helpers
│   ├── version.jq                     # JQ version processing
│   └── ansi.jq                        # ANSI color output helpers
├── extensions/                        # Extension metadata schema
│   ├── extensions_schema.yaml         # Schema for extension definitions
│   └── BUILD
├── check_repositories.sh             # Enforce HTTP repos and SHA256 sums
└── update_crates.sh                   # Regenerate Rust crate definitions
```

---

## `tools/dependency/` — Dependency Validation

### `validate.py`

The primary dependency validation tool. It checks that all external dependencies declared in `repository_locations.bzl` files meet Envoy's standards:

- Every dependency has a pinned version with SHA256 hash
- Metadata includes project URL, license, and last update date
- Version strings match expected patterns
- No git-based dependencies (must use HTTP archives)
- Dependencies conform to the schema defined in `api/bazel/external_deps.bzl`

**Usage:**

```bash
bazel run //tools/dependency:validate
```

### `validate_test.py`

Unit tests for the validation logic, ensuring the validator itself correctly identifies conforming and non-conforming dependency entries.

---

## CVE Tracking

Envoy tracks known CVEs (Common Vulnerabilities and Exposures) in its dependencies to ensure timely patching.

### `cve_fetch.py`

Fetches CVE data from public vulnerability databases (NVD, GitHub Advisory Database) for all declared dependencies. Matches CVEs against the specific versions pinned in `repository_locations.bzl`.

### `cve_update.sh`

Updates the local CVE database by re-fetching vulnerability data. Should be run periodically or before security reviews.

### `cves.sh`

Generates a human-readable CVE summary report showing:
- Dependencies with known vulnerabilities
- Severity levels
- Whether patches are available
- Recommended actions

### `cve.yaml`

Configuration for the CVE system, including:
- Known false positives to suppress
- Dependencies to skip (e.g., vendored forks with patches applied)
- Severity thresholds for reporting

### JQ Filters

- **`cve_matcher.jq`** — Matches CVE entries against dependency versions
- **`cve_report.jq`** — Formats CVE data into readable reports
- **`cve_utils.jq`** — Shared utility functions for CVE processing

---

## `tools/dependency/ossf_scorecard.py` — OSSF Scorecard

Integrates with the [OpenSSF Scorecard](https://securityscorecards.dev/) project to assess the security posture of Envoy's upstream dependencies. Scorecard evaluates projects on criteria like:

- Branch protection rules
- CI/CD test coverage
- Dependency pinning
- Signed releases
- Vulnerability disclosure process

---

## `tools/check_repositories.sh` — Repository Enforcement

Enforces two key rules across all `.bzl` files that declare external repositories:

1. **No git repositories** — all dependencies must use HTTP archives (tarballs), not git clones. HTTP archives are reproducible and cacheable.
2. **SHA256 sums required** — every downloaded archive must have a SHA256 hash for integrity verification.

**Usage:**

```bash
tools/check_repositories.sh
```

---

## `tools/update_crates.sh` — Rust Crate Management

Regenerates Rust crate definitions via `cargo-raze`. This is used when:
- Adding a new Rust dependency
- Updating existing Rust dependency versions
- Syncing the Bazel build with `Cargo.toml` changes

**Usage:**

```bash
tools/update_crates.sh
```

---

## `tools/extensions/` — Extension Schema

### `extensions_schema.yaml`

Defines the metadata schema for all Envoy extensions. Every extension in the repository must conform to this schema.

**Key sections:**

- **`builtin`** — Extensions built into the core binary (not optional)
- **`security_postures`** — Trust classification for extensions:
  - `robust_to_untrusted_downstream` — Safe with untrusted client traffic
  - `robust_to_untrusted_downstream_and_upstream` — Safe with untrusted traffic in both directions
  - `requires_trusted_downstream_and_upstream` — Only for fully trusted environments
  - `data_plane_agnostic` — Not relevant to data plane threats (e.g., stats sinks)
  - `unknown` — Placeholder for unclassified extensions
- **`categories`** — All valid extension categories (e.g., `envoy.filters.http`, `envoy.access_loggers`, `envoy.transport_sockets.upstream`)
- **`status_values`** — Maturity levels:
  - `stable` — Production-ready
  - `alpha` — Functional but not battle-tested
  - `wip` — Work in progress, not for production

This schema is consumed by the build system and documentation generators to ensure every extension is properly classified and documented.

---

## Dependency Management Workflow

```
Developer adds/updates a dependency
        │
        ├── Update repository_locations.bzl with version + SHA256
        │
        ├── tools/check_repositories.sh ──→ Verify HTTP + SHA256
        │
        ├── tools/dependency/validate.py ──→ Validate metadata
        │
        ├── tools/dependency/cve_fetch.py ──→ Check for known CVEs
        │
        └── CI runs all checks automatically on PR
```

---

## Navigation

| Previous | Up | Next |
|----------|------|------|
| [05-Debugging-and-Profiling-Tools.md](05-Debugging-and-Profiling-Tools.md) | [01-Overview.md](01-Overview.md) | [07-IDE-and-Editor-Tools.md](07-IDE-and-Editor-Tools.md) |
