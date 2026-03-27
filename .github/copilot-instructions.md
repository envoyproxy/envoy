# GitHub Copilot instructions for envoyproxy/envoy

**Note:** These instructions are specifically for GitHub Copilot's agent/workspace environment, not for developers using Copilot in VS Code or other IDEs. For general development setup, see [DEVELOPER.md](https://github.com/envoyproxy/envoy/blob/main/DEVELOPER.md).

## Repository overview

This repository contains the Envoy proxy, a cloud-native high-performance edge/middle/service proxy. It includes:

- **source/** - Core C++ implementation of Envoy proxy
- **test/** - Comprehensive test suite including unit, integration, and performance tests
- **api/** - Protocol buffer definitions for Envoy's APIs (xDS, filters, etc.)
- **bazel/** - Bazel build configurations, rules, and toolchain definitions
- **contrib/** - Community-contributed extensions
- **mobile/** - Envoy Mobile implementation
- **tools/** - Development tooling, scripts, and utilities
- **docs/** - Documentation source files

## Build system

This repository uses **Bazel** as its primary build system.

### Running Bazel commands in Copilot's local environment

**IMPORTANT:** The key capability for GitHub Copilot is to run Bazel commands directly in the local workspace to build, test, and validate changes.
**IMPORTANT:** You MUST run `/tmp/fix-bazel-truststore.sh` before the first time you issue a bazel command!!!
**IMPORTANT:** Many bazel targets will be very slow/expensive to run/build/test - only test with bazel when told to.

### Bazel configuration files

- `.bazelrc` - Repository-wide Bazel configuration with build flags and platform settings
- `user.bazelrc` - Optional user-specific overrides (gitignored)
- `.bazelversion` - Specifies the exact Bazel version to use
- `MODULE.bazel` / `WORKSPACE` - Dependency definitions (using bzlmod and WORKSPACE modes)

### Compiler configuration

Envoy supports multiple compiler configurations. **Use `--config=clang` by default** unless told otherwise:

```bash
# Use Clang with libc++ (recommended, use by default)
bazel build --config=clang //source/exe:envoy-static

# Use GCC with libstdc++ (only if explicitly requested)
bazel build --config=gcc //source/exe:envoy-static
```

## Language and coding standards

### C++

- **Primary Language:** Modern C++ (C++20)
- **Compiler Requirements:** Clang >= 18 or GCC >= 13
- **Standard Library:** libc++ (with Clang) or libstdc++ (with GCC)
- **Style Guide:** See [STYLE.md](https://github.com/envoyproxy/envoy/blob/main/STYLE.md) for comprehensive coding standards

## Testing

### Running tests

```bash
# Run all tests
bazel test --config=clang //test/...

# Run tests in a specific directory
bazel test --config=clang //test/common/http/...

# Run a single test target
bazel test --config=clang //test/common/http:async_client_impl_test

# Run tests with additional logging
bazel test --config=clang --test_output=streamed //test/... --test_arg="--" --test_arg="-l trace"

# Run tests with IPv4 only
bazel test --config=clang //test/... --test_env=ENVOY_IP_TEST_VERSIONS=v4only

# Run tests with IPv6 only
bazel test --config=clang //test/... --test_env=ENVOY_IP_TEST_VERSIONS=v6only

# Disable heap checker
bazel test --config=clang //test/... --test_env=HEAPCHECK=
```

## Dependencies

### Dependency locations

Depdendencies are configured in `bazel/repository_locations.bzl`, for API deps its `api/bazel/repository_locations.bzl`

See `bazel/repositories.bzl` for setup - eg this is where any patching is controlled.

If you need to create or update a patch - do the following:

- checkout the upstream repo at the correct version/commit
- apply any existing patches
- make changes
- diff the changes to the patch file

Pay attention to how the patch_args are setup in repositories.bzl - some are p0, while others are p1. Prefer p1 when
creating new patches.


## Code formatting and linting

### Format code

```bash
# Check and fix formatting (recommended for source/, test/, contrib/ changes)
bazel run --config=clang //tools/code_format:check_format -- fix

# Quick format check (much faster, doesn't fix)
bazel run --config=clang //tools/code:check

# Check format without fixing
bazel run --config=clang //tools/code_format:check_format -- check

# Format API files
bazel run --config=clang //tools/proto_format:proto_format -- fix
```

### Dependency validation

**Always run dependency checks when adding or updating dependencies:**

```bash
# Validate dependency metadata
bazel run --config=clang //tools/dependency:validate

# Run dependency tests
bazel run --config=clang //tools/dependency:validate_test

# Check for dependency setup/updates
# -v warn: verbosity level, -c release_dates: check release dates, releases: check type
bazel run --config=clang //tools/dependency:check -- -v warn -c release_dates releases
```

## Development workflow

### Making changes

1. **Understand the codebase:**
   - Review [DEVELOPER.md](https://github.com/envoyproxy/envoy/blob/main/DEVELOPER.md) for development guidelines
   - Check [REPO_LAYOUT.md](https://github.com/envoyproxy/envoy/blob/main/REPO_LAYOUT.md) for repository organization
   - Read [CONTRIBUTING.md](https://github.com/envoyproxy/envoy/blob/main/CONTRIBUTING.md) for contribution guidelines

2. **Build and test locally:**
   ```bash
   # Build Envoy (this is slow/expensive)
   bazel build --config=clang //source/exe:envoy-static

   # Run relevant tests (often slow/expensive - depending on test)
   bazel test --config=clang //test/path/to/relevant/tests/...

   # Quick format check
   bazel run --config=clang //tools/code:check
   ```

3. **Run Envoy locally:**

   Unless you're testing a build, download a pre-built binary from the [releases page](https://github.com/envoyproxy/envoy/releases):

   ```bash
   # Download latest release (recommended for testing)
   wget https://github.com/envoyproxy/envoy/releases/latest/download/envoy-static-linux-x86_64
   chmod +x envoy-static-linux-x86_64
   ./envoy-static-linux-x86_64 --config-path /path/to/config.yaml

   # Or after building locally (takes too long, only if needed)
   ./bazel-bin/source/exe/envoy-static --config-path /path/to/config.yaml
   ```

## Common development tasks

### Adding or updating dependencies

1. Check [bazel/repository_locations.bzl](https://github.com/envoyproxy/envoy/blob/main/bazel/repository_locations.bzl) for existing dependencies
2. See [bazel/EXTERNAL_DEPS.md](https://github.com/envoyproxy/envoy/blob/main/bazel/EXTERNAL_DEPS.md) for how to add/update dependencies
3. **Always run dependency validation after changes:**
   ```bash
   # Validate dependency metadata and relationships
   bazel run --config=clang //tools/dependency:validate

   # Run all dependency checks (recommended)
   ./ci/do_ci.sh deps
   ```

## CI and testing

### Using CI scripts vs direct Bazel

These are the scripts that are run in CI. They also setup the environment and set the
toolchain config.

You will have been provided a `user.bazelrc` that should have startup args matching what
using `do_ci.sh` would provide. This ensures dependencies are not re-downloaded.

#### CI script targets

```bash
# Run all formatting and pre-checks
./ci/do_ci.sh format

# Run dependency checks (validation + CVE scanning)
./ci/do_ci.sh deps

# Development build (compile and test)
./ci/do_ci.sh dev

# Release testing (as done by CI)
./ci/do_ci.sh release.test_only [OPTIONAL TEST TARGETS]

# Release build
./ci/do_ci.sh release.server_only

```

#### When to use direct Bazel

Use direct `bazel` commands for:
- **Targeted builds/tests** - Building or testing specific targets
- **Iterative development** - Quick rebuilds during active development
- **Custom configurations** - When you need specific flags not covered by CI scripts
