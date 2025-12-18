# SSL Backend Selection Support

This document describes how to build Envoy with either BoringSSL (upstream default) or OpenSSL (via bssl-compat).

## Overview

The build system now supports selecting between two SSL backends:
- **BoringSSL** (default) - The upstream SSL library used by Envoy
- **OpenSSL** (via bssl-compat) - OpenSSL 3.0.x with a BoringSSL compatibility layer

## Usage

### Building with BoringSSL (Default)

Simply build as normal without any special flags:

```bash
bazel build //source/exe:envoy-static
```

### Building with OpenSSL

Use the `--config=openssl` flag (recommended):

```bash
bazel build --config=openssl //source/exe:envoy-static
```

This automatically:
- Sets `--define=ssl=openssl` to select the OpenSSL backend
- Adds `-DENVOY_SSL_OPENSSL` compiler flag
- Disables QUIC/HTTP3 support
- Restricts tests to IPv4 only

Alternatively, you can use just the define flag (not recommended as it doesn't apply other OpenSSL-specific settings):

```bash
bazel build --define=ssl=openssl //source/exe:envoy-static
```

## Implementation Details

### Build System Changes

1. **Config Settings** (`bazel/BUILD`):
   - Added `ssl_openssl` config_setting for `--define=ssl=openssl`
   - Added `ssl_boringssl` config_setting for `--define=ssl=boringssl`

2. **Aliases** (`bazel/BUILD`):
   - `//bazel:boringssl` now conditionally points to either `@boringssl//:ssl` or `@bssl-compat//:ssl`
   - `//bazel:boringcrypto` now conditionally points to either `@boringssl//:crypto` or `@bssl-compat//:crypto`

3. **Dependencies** (`bazel/repositories.bzl`):
   - Both BoringSSL and OpenSSL (via bssl-compat) are loaded
   - Selection happens at build time via the aliases

4. **Helper Function** (`bazel/envoy_select.bzl`):
   - Added `envoy_select_ssl()` for conditional compilation based on SSL backend
   - Example: `envoy_select_ssl(if_openssl = [...], if_boringssl = [...])`

5. **Preprocessor Defines**:
   - When `ssl=openssl` is set, `-DENVOY_SSL_OPENSSL` is added to compilation flags
   - This allows source code to conditionally compile OpenSSL-specific code

### Source Code Changes

OpenSSL-specific code is wrapped with `#ifdef ENVOY_SSL_OPENSSL`:

1. **SSL Socket** (`source/common/tls/ssl_socket.cc`):
   - EAGAIN handling in `SSL_ERROR_SSL` cases

2. **Context Implementation** (`source/common/tls/context_impl.cc`):
   - Disabled `SSL_CTX_set_reverify_on_resume()` (not in bssl-compat)
   - Disabled `SSL_was_key_usage_invalid()` (not in bssl-compat)

3. **BIO Handling** (`source/common/tls/io_handle_bio.cc`):
   - Added `io_handle_new()` and `io_handle_free()` for OpenSSL
   - Added BIO control commands for OpenSSL

4. **Version Reporting** (`source/common/version/BUILD`):
   - Reports "OpenSSL" or "BoringSSL" based on build configuration

### Configuration Files

The `openssl/bazelrc` file automatically:
- Sets `--define=ssl=openssl`
- Adds `-DENVOY_SSL_OPENSSL` compiler flag
- Disables QUIC/HTTP3 support
- Restricts tests to IPv4 only

## Test Considerations

Some tests have different expectations for OpenSSL vs BoringSSL:
- TLS fingerprints may differ
- Cipher suite ordering may differ
- Alert codes may differ

Tests should use `#ifdef ENVOY_SSL_OPENSSL` to adjust expectations accordingly.

## Migration Guide

### For Users

- To use BoringSSL: No changes needed (default behavior)
- To use OpenSSL: Add `--define=ssl=openssl` to your build commands

### For Developers

When adding SSL-related code:

1. **If the code works with both backends**: No special handling needed

2. **If the code is OpenSSL-specific**: Wrap it with:
   ```cpp
   #ifdef ENVOY_SSL_OPENSSL
   // OpenSSL-specific code here
   #endif
   ```

3. **If the code is BoringSSL-specific**: Wrap it with:
   ```cpp
   #ifndef ENVOY_SSL_OPENSSL
   // BoringSSL-specific code here
   #endif
   ```

4. **For conditional build dependencies**: Use `envoy_select_ssl()` in BUILD files:
   ```python
   deps = [...] + envoy_select_ssl(
       if_openssl = ["//openssl/specific:dep"],
       if_boringssl = ["//boringssl/specific:dep"],
   )
   ```

## Limitations

When building with OpenSSL:
- QUIC/HTTP3 support is disabled (OpenSSL doesn't provide QUIC implementation)
- Some BoringSSL-specific APIs are not available
- Test coverage may differ from BoringSSL builds
