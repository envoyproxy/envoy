"""Rules for detecting and failing on deprecated build flags."""

def _check_removed_fips_define_impl(ctx):
    """Check if the deprecated --define boringssl=fips is set"""
    if ctx.var.get("boringssl") == "fips":
        fail("""
================================================================================
ERROR: --define boringssl=fips is deprecated and no longer supported.

Please use one of the new config options instead:

  For BoringSSL FIPS (Linux x86_64 only):
    bazel build --config=boringssl-fips //source/exe:envoy-static

  For AWS-LC FIPS (Linux x86_64, aarch64, ppc64le):
    bazel build --config=aws-lc-fips //source/exe:envoy-static

See bazel/SSL.md for more details.
================================================================================
""")
    return []

check_removed_fips_define = rule(
    implementation = _check_removed_fips_define_impl,
    build_setting = config.string(flag = True),
)
