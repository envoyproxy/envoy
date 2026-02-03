# SSL library configuration

Envoy uses [BoringSSL](https://github.com/google/boringssl) as its default SSL library.

For FIPS-compliant builds, Envoy supports both BoringSSL-FIPS and [AWS-LC](https://github.com/aws/aws-lc) FIPS,
which provides FIPS support for the aarch64 and ppc64le architectures.

## Default (non-FIPS)

No configuration needed. Envoy builds with standard BoringSSL by default:

```bash
bazel build //source/exe:envoy-static
```

## FIPS builds

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
