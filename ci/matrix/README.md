# CI Matrix Tests

This directory contains tests that verify Envoy's toolchain detection and selection behavior across different compiler configurations. These tests ensure that Bazel correctly identifies and uses the appropriate compiler toolchains (GCC vs Clang) based on various configuration methods.

> **NOTE**: If running these tests locally, please ensure that you do not have a `user.bazelrc` file as this may interfere with the tests. The tests rely on specific default behavior that can be altered by user-specific Bazel configurations.

## What is Being Tested

The test suite validates toolchain behavior in the following scenarios:

- **Default build**: Testing what toolchain is selected with no explicit configuration
- **Config-based selection**: Testing `--config=clang-libc++` and `--config=gcc` flags
- **Environment-based selection**: Testing `CC`/`CXX` environment variable overrides
- **Compiler availability**: Testing behavior when only specific compilers are available

The tests run against different Docker environments:
- `gcc`: Only GCC available
- `llvm`: Only LLVM/Clang available
- `all`: Both compilers available
- `none`: No compilers available (tests hermetic toolchain fallback)

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
✅ CLANG passed as expected
✅ GCC_ENV passed as expected
✅ CLANG_ENV passed as expected
All test configs passed as expected
```

Failed tests will show which configuration didn't match expectations:
```
❌ CLANG: expected=clang-libc++, got=fail
```

### Troubleshooting

- If you get permission errors, ensure `UID` and `GID` environment variables are set correctly
- If tests fail unexpectedly, check that no local Bazel configuration files are interfering
- The `none` configuration is expected to fail until hermetic toolchains are fully implemented
