# GitHub Copilot instructions for envoyproxy/envoy

**Note:** These instructions are specifically for GitHub Copilot's cloud agent environment, not for
developers using Copilot in VS Code or other IDEs.

**Read [AGENTS.md](../AGENTS.md) first.** It is the canonical guide for AI coding agents in this
repository (critical rules, coding/testing conventions, format checks, release notes, runtime
guards, dependencies and PR workflow). This file only adds what is specific to the Copilot agent
environment.

## Copilot environment setup

The environment is prepared by
[copilot-setup-steps.yml](workflows/copilot-setup-steps.yml).

- **IMPORTANT:** You MUST run `/tmp/fix-bazel-truststore.sh` before the first time you issue a
  bazel command!!!
- A `user.bazelrc` is provided with startup args (truststore, output base) matching what
  `ci/do_ci.sh` would use, so dependencies are not re-downloaded. Do not remove it.
- **IMPORTANT:** Many bazel targets are very slow/expensive to build/test. Only build or test with
  bazel when told to, and prefer the narrowest target that covers the change.

## Toolchain

Use the default toolchain (Clang with libc++, autodetected) unless told otherwise:

```bash
bazel build //source/exe:envoy-static

# GCC with libstdc++ (only if explicitly requested)
bazel build --config=gcc //source/exe:envoy-static
```

## Running tests

```bash
# Run tests in a specific directory
bazel test //test/common/http/...

# Run a single test target
bazel test //test/common/http:async_client_impl_test

# Run tests with additional logging
bazel test --test_output=streamed //test/common/http:async_client_impl_test \
  --test_arg="--" --test_arg="-l trace"

# Run tests with IPv4 or IPv6 only
bazel test //test/common/http/... --test_env=ENVOY_IP_TEST_VERSIONS=v4only
bazel test //test/common/http/... --test_env=ENVOY_IP_TEST_VERSIONS=v6only

# Disable heap checker
bazel test //test/common/http/... --test_env=HEAPCHECK=
```

## Quick checks

```bash
# Quick format check (much faster than check_format, doesn't fix)
bazel run //tools/code:check

# Dependency metadata checks (validation, reachability, registry pin, lockfiles)
./ci/do_ci.sh deps
```

See the format and dependency sections of [AGENTS.md](../AGENTS.md) for the full commands.

## CI script targets

These are the scripts run in CI; they also set up the environment and toolchain config. Use direct
`bazel` commands for targeted builds/tests and iterative development.

```bash
./ci/do_ci.sh format                                    # all formatting and pre-checks
./ci/do_ci.sh deps                                      # dependency checks
./ci/do_ci.sh dev                                       # development build (compile and test)
./ci/do_ci.sh release.test_only [OPTIONAL TEST TARGETS] # release testing (as done by CI)
./ci/do_ci.sh release.server_only                       # release build
```

## Running Envoy

Unless you are testing a local build, use a pre-built binary from the
[releases page](https://github.com/envoyproxy/envoy/releases):

```bash
wget https://github.com/envoyproxy/envoy/releases/latest/download/envoy-static-linux-x86_64
chmod +x envoy-static-linux-x86_64
./envoy-static-linux-x86_64 --config-path /path/to/config.yaml

# Or after building locally (slow, only if needed)
./bazel-bin/source/exe/envoy-static --config-path /path/to/config.yaml
```
