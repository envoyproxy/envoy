# SSL library configuration

Envoy uses [BoringSSL](https://github.com/google/boringssl) as its default SSL library. [OpenSSL](https://github.com/openssl/openssl) is also supported by the build system as an alternative SSL library.

For FIPS-compliant builds, Envoy supports both BoringSSL-FIPS and [AWS-LC](https://github.com/aws/aws-lc) FIPS,
which provides FIPS support for the aarch64 and ppc64le architectures.

## Default (non-FIPS)

No configuration needed. Envoy builds with standard BoringSSL by default:

```bash
bazel build //source/exe:envoy-static
```

## FIPS builds

### Supported FIPS builds and architectures

At this time, only the BoringSSL FIPS build on x86_64 is supported and tested by the Envoy project.

We are happy to accept patches to allow Envoy builds with other libraries or architectures, but
the responsibility for maintenance, and resolving incompatibility remains with dowstream projects.

### BoringSSL-FIPS

```bash
bazel build --config=boringssl-fips //source/exe:envoy-static
```

- **Supported architectures:** Linux x86_64 only
- **Version string:** `BoringSSL-FIPS` (visible in `envoy --version`)

### AWS-LC FIPS

```bash
bazel build --config=aws-lc-fips //source/exe:envoy-static
```

- **Supported architectures:** Linux x86_64, aarch64, ppc64le
- **Version string:** `AWS-LC-FIPS` (visible in `envoy --version`)
- **Note:** HTTP/3 (QUIC) is disabled for AWS-LC builds

## OpenSSL

BoringSSL is the supported and default SSL implementation in Envoy. OpenSSL is offered as an alternative.

Differently from the other SSL implementations supported by Envoy, OpenSSL libraries are not statically linked into the Envoy binary. OpenSSL libraries (version 3.5 or higher) must be present at runtime. The current OpenSSL implementation will load them dynamically with `dlopen()`.

FIPS mode in OpenSSL is enforced at runtime - not build time - through OpenSSL and/or operating system configuration.

In order to build Envoy using OpenSSL instead of BoringSSL, run:

```bash
bazel build --config=openssl //source/exe:envoy-static
```

- **Supported architectures:** Linux x86_64, aarch64, ppc64le
- **Version string:** `OpenSSL` (visible in `envoy --version`)
- **Note:** HTTP/3 (QUIC) is disabled for OpenSSL builds

**NOTE:** Because of the dynamic linking method, rebuilding Envoy due to a security vulnerability (CVE) affecting OpenSSL should not be necessary. This leaves the responsibility of handling any security issue with the runtime system administrators to keep their system up to date. **Thus, Envoy builds with OpenSSL are not covered by [Envoy Security Policy](../SECURITY.md)**.

## Migration from `--define boringssl=fips`

The legacy `--define boringssl=fips` flag no longer works. Migrate as follows:

| Legacy | New |
|--------|-----|
| `--define boringssl=fips` | `--config=boringssl-fips` |
| `--define boringssl=fips` (on ppc64le) | `--config=aws-lc-fips` |

The legacy flag automatically selected AWS-LC on ppc64le. With the new approach, you must explicitly choose the library.

## SSL flag integrity

The Bazel SSL configuration uses three interdependent flags: `//bazel:ssl`, `//bazel:crypto`, and `//bazel:fips`.

**Do not set these flags directly.** Use the `--config` options above, which ensure the flags are set consistently.

Inconsistent flag combinations (e.g., a FIPS library with `--//bazel:fips=False`, or mismatched `ssl`/`crypto` libraries) will produce broken builds or incorrect version strings.

## Verifying FIPS build

Check the SSL library in use:

```bash
envoy --version
```

Look for:

- `BoringSSL-FIPS` — BoringSSL FIPS build
- `AWS-LC-FIPS` — AWS-LC FIPS build
- `BoringSSL` — Standard (non-FIPS) build
- `OpenSSL` — OpenSSL build
