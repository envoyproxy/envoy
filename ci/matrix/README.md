# CI Matrix Tests

This directory contains tests that verify Envoy's toolchain detection and selection behavior across different compiler configurations. These tests ensure that Bazel correctly identifies and uses the appropriate compiler toolchains (GCC vs Clang) based on various configuration methods.

> **NOTE**: If running these tests locally, please ensure that you do not have `ci.bazelrc` or `user.bazelrc` files in the repository root, as these may interfere with the tests. The tests rely on specific default behavior that can be altered by local Bazel configurations.

## What is Being Tested

The test suite validates toolchain behavior in the following scenarios:

- **Default build**: Testing what toolchain is selected with no explicit configuration
- **Config-based selection**: Testing the default toolchain (clang + libc++) and `--config=gcc` (gcc + libstdc++)
- **Environment-based selection**: Testing `CC`/`CXX` environment variable overrides
- **Compiler availability**: Both LLVM and GCC work hermetically, regardless of installed compilers
- **Resolution identity**: Checking the resolved compiler, toolchain identifier, and hermetic compiler path
- **GCC configuration**: Checking that `//bazel:gcc_build` matches only with `--config=gcc`
- **Host GCC override**: A module fixture opts into `/usr` explicitly, with and without
  `gcc_version`, and rejects a version that does not match the installed compiler
- **Alternate GCC version**: The fixture also selects GCC 14.3.0 instead of the
  main module's default, checking both resolution and the compiled compiler version

The tests run against different Docker environments:
- `gcc`: Host GCC installed
- `llvm`: LLVM runtime prerequisites installed, without host GCC
- `all`: Both sets of host prerequisites installed
- `none`: No host compilers installed

Every environment must use hermetic LLVM by default and hermetic GCC with `--config=gcc`.
Setting `CC`/`CXX` does not change that selection.
The `gcc` and `all` environments also test the explicit host override and alternate GCC
version using a temporary module copy inside `ci/matrix`. Source directories are symlinked
into the fixture, and it is removed when the test finishes. Envoy remains the root module
because the upstream compiler extensions do not support being configured transitively.

## Running Tests Locally

### Prerequisites

- Docker and Docker Compose
- Make sure your user has Docker permissions

### Running Specific Test Configurations

```bash

# ensure correct permissions
export UID
export GID

# Test only GCC configuration
docker compose -f ci/matrix/docker-compose.yml run --build gcc

# Test only LLVM configuration
docker compose -f ci/matrix/docker-compose.yml run --build llvm

# Test both compilers available
docker compose -f ci/matrix/docker-compose.yml run --build all

# Test no compilers (hermetic toolchains)
docker compose -f ci/matrix/docker-compose.yml run --build none
```

## Test Output

Successful tests will show output like:
```
✅ NO_ARGS passed as expected
✅ GCC passed as expected
✅ GCC_ENV passed as expected
✅ CLANG_ENV passed as expected
All test configs passed as expected
```

Failed tests will show which configuration didn't match expectations:
```
❌ GCC: expected=gcc-libstdc++, got=fail
```

### Troubleshooting

- If you get permission errors, ensure `UID` and `GID` environment variables are set correctly
- If tests fail unexpectedly, check that no local Bazel configuration files are interfering
