# Bzlmod CI/CD Validation

This document describes the CI/CD validation setup for Envoy's bzlmod migration.

## Overview

Per best practices, bzlmod build validation is performed in CI/CD pipelines, not local scripts. This ensures consistent validation across all contributors and prevents merge of broken bzlmod configurations.

## CI/CD Systems

### 1. GitHub Actions Workflow (`.github/workflows/bzlmod-validation.yml`)

Primary validation workflow that runs on:
- Push to `main` and `release/**` branches
- Pull requests that modify bzlmod-related files:
  - `MODULE.bazel`
  - `api/MODULE.bazel`
  - `mobile/MODULE.bazel`
  - `bazel/extensions/**`
  - `.bazelversion`

**Jobs:**

#### `validate-bzlmod`
Validates core bzlmod functionality:
- Dependency graph resolution (`bazel mod graph`)
- Core module builds (`//source/common/common:assert_lib`)
- API module builds (`@envoy_api//envoy/config/core/v3:pkg`)
- Mobile module queries
- Sample tests
- `bazel mod tidy` validation

#### `validate-extensions`
Validates module extensions:
- Core extension functionality
- Repository mappings
- Extension-generated repositories

#### `validate-sub-modules`
Validates sub-modules independently:
- API sub-module (`api/MODULE.bazel`)
- Mobile sub-module (`mobile/MODULE.bazel`)

#### `summary`
Aggregates results from all validation jobs and provides clear pass/fail status.

### 2. Bazel CI (`.bazelci/presubmit.yml`)

Bazel's official CI system includes a dedicated bzlmod validation task:

```yaml
bzlmod_validation:
  name: "Bzlmod Validation"
  platform: ubuntu2004
  build_targets:
  - "//source/common/common:assert_lib"
  - "@envoy_api//envoy/config/core/v3:pkg"
  test_targets:
  - "//test/common/common:assert_test"
  build_flags:
  - "--enable_bzlmod"
  test_flags:
  - "--enable_bzlmod"
```

This runs on Bazel's infrastructure and provides additional validation coverage.

## Bazel Configuration (`.bazelrc`)

Bzlmod is **enabled by default** in `.bazelrc`:

```starlark
# Enable bzlmod by default
common --enable_bzlmod

# Bzlmod-specific configuration
build:bzlmod --enable_bzlmod
test:bzlmod --enable_bzlmod
query:bzlmod --enable_bzlmod

# Legacy WORKSPACE mode (deprecated)
build:workspace --noenable_bzlmod
test:workspace --noenable_bzlmod
```

### Usage

**Default (bzlmod):**
```bash
bazel build //...
bazel test //...
```

**Explicit bzlmod config:**
```bash
bazel build --config=bzlmod //...
```

**Legacy WORKSPACE mode (deprecated):**
```bash
bazel build --config=workspace //...
```

## Validation Scope

### What is Validated

✅ **MODULE.bazel dependency resolution**
- All dependencies resolve correctly
- No circular dependencies
- BCR dependencies available

✅ **Core module builds**
- Essential targets build successfully
- No missing dependencies

✅ **API module functionality**
- API targets build correctly
- Proto generation works

✅ **Mobile module structure**
- Mobile repositories accessible
- Module structure valid

✅ **Module extensions**
- Extensions execute without errors
- Repositories created correctly
- No duplicate repository definitions

✅ **Sub-module independence**
- API sub-module standalone functionality
- Mobile sub-module standalone functionality

✅ **bazel mod tidy compatibility**
- MODULE.bazel format compatibility
- Automated maintenance readiness

### What is NOT Validated

❌ **Full integration tests** - These run in separate, comprehensive CI pipelines

❌ **Performance benchmarks** - Separate performance testing infrastructure

❌ **Cross-platform builds** - Validated in platform-specific CI jobs

❌ **WORKSPACE mode** - Deprecated and not validated

## Troubleshooting CI Failures

### "Dependency resolution failed"
- Check MODULE.bazel syntax
- Verify BCR dependencies are available
- Check extension implementation

### "Build failed with bzlmod"
- Verify targets exist
- Check dependency declarations
- Review extension output

### "bazel mod tidy made changes"
- Run `bazel mod tidy --enable_bzlmod` locally
- Commit the changes
- This is informational, not a failure

### "Extension execution failed"
- Check extension implementation in `bazel/extensions/`
- Verify repository rules
- Review extension documentation

## Local Validation

While CI/CD is the authoritative validation, developers can run similar checks locally:

```bash
# Validate dependency resolution
bazel mod graph --enable_bzlmod

# Build core components
bazel build --enable_bzlmod //source/common/common:assert_lib

# Build API module
bazel build --enable_bzlmod @envoy_api//envoy/config/core/v3:pkg

# Run tests
bazel test --enable_bzlmod //test/common/common:assert_test

# Validate bazel mod tidy
bazel mod tidy --enable_bzlmod
```

## Adding New Validation

To add new bzlmod validation:

1. **For new targets:** Add to `.github/workflows/bzlmod-validation.yml`
2. **For Bazel CI:** Add to `.bazelci/presubmit.yml`
3. **For new extensions:** Add validation in `validate-extensions` job
4. **For new sub-modules:** Add to `validate-sub-modules` job

## Maintenance

### Updating Bazel Version

When updating `.bazelversion`:
1. CI automatically uses new version (via Bazelisk)
2. Test locally first
3. Update documentation if behavior changes

### Updating Dependencies

When updating `MODULE.bazel`:
1. CI validates changes automatically
2. Check `bazel mod graph` output
3. Run `bazel mod tidy` if needed

### Updating Extensions

When modifying `bazel/extensions/`:
1. CI validates extension execution
2. Test locally with `--enable_bzlmod`
3. Verify repository creation

## Future Improvements

Planned enhancements to CI/CD validation:

- [ ] Add caching for faster CI runs
- [ ] Parallel validation of independent modules
- [ ] Integration test subset with bzlmod
- [ ] Performance comparison (bzlmod vs WORKSPACE)
- [ ] Automated MODULE.bazel updates via Renovate/Dependabot

## References

- **BZLMOD_MIGRATION_GUIDE.md** - User guide for bzlmod
- **BAZEL8_UPGRADE.md** - Bazel 8 upgrade details
- **BZLMOD_STATUS.md** - Current status and commands
- **BZLMOD_MIGRATION_REVIEW.md** - Expert review
- [Bazel CI Documentation](https://github.com/bazelbuild/continuous-integration)
- [GitHub Actions Documentation](https://docs.github.com/en/actions)

## Contact

For questions about CI/CD setup:
- File issues with `[ci]` or `[bzlmod]` prefix
- Check existing CI/CD workflows in `.github/workflows/`
- Review Bazel CI config in `.bazelci/presubmit.yml`
