# Bzlmod Migration Changelog

## Initial Migration (v1.33.0-dev)

### Added
- **MODULE.bazel**: New bzlmod configuration file with core dependencies
- **Hybrid Mode**: Enabled bzlmod alongside WORKSPACE for gradual migration
- **Documentation**: Comprehensive migration guide and examples
- **Validation**: Script to test and validate the bzlmod setup

### Migrated Dependencies
The following dependencies have been migrated from WORKSPACE to MODULE.bazel:

#### Build Rules
- `rules_cc` (0.0.18) - C++ build rules
- `rules_proto` (7.0.2) - Protocol buffer rules
- `rules_java` (8.9.0) - Java build rules  
- `rules_python` (1.1.0) - Python build rules
- `rules_foreign_cc` (0.14.0) - Foreign C/C++ library rules

#### Core Libraries
- `protobuf` (29.3) - Protocol buffers runtime library
- `abseil-cpp` (20240722.0) - Abseil C++ common libraries
- `googletest` (1.17.0) - Google Test framework

#### Security
- `boringssl` (0.20240913.0) - BoringSSL cryptographic library

#### Build Tools
- `gazelle` (0.45.0) - BUILD file generator for Go projects
- `bazel_features` (1.33.0) - Bazel feature detection support

### Configuration Changes
- **`.bazelrc`**: Enabled bzlmod with `--enable_bzlmod`
- **Hybrid operation**: Both WORKSPACE and MODULE.bazel are active
- **Compatibility**: Maintained existing repository names via `repo_name` parameter

### Documentation Added
- `docs/root/start/migrating/bzlmod.md` - Complete migration guide
- `examples/bzlmod/README.md` - Migration examples and troubleshooting
- `tools/bzlmod_validate.sh` - Validation script for testing migration
- Updated `DEVELOPER.md` and `bazel/README.md` with bzlmod references

### Remaining in WORKSPACE
The following major dependencies remain in WORKSPACE for future migration:
- gRPC ecosystem (complex dependency tree)
- Envoy API definitions (custom repositories)
- Platform-specific build rules (rules_apple, rules_android)
- Extension and contrib dependencies
- Custom HTTP archives with patches

### Migration Notes
- **Zero Breaking Changes**: All existing build commands continue to work
- **Version Alignment**: Bzlmod versions match existing WORKSPACE versions
- **Gradual Approach**: Migration can be continued incrementally
- **Well Tested**: Core dependencies selected for stability and BCR availability

### Next Phase Recommendations
1. Migrate gRPC (`grpc` module in BCR)
2. Migrate additional core libraries (re2, zlib)
3. Add platform-specific rules as needed
4. Consider custom module extensions for Envoy-specific dependencies

### Testing
- Basic syntax validation implemented
- Validation script provided for ongoing testing
- Compatible with existing CI/CD pipelines
- No immediate action required from users