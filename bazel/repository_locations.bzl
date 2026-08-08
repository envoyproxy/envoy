# This should match the schema defined in external_deps.bzl.

REPOSITORY_LOCATIONS_SPEC = dict(
    # BZLMOD: NEEDS UPDATE
    quiche = dict(
        version = "89d6d17edc0f0b79f38edf6fac9e5c8bf5f3cfd7",
        sha256 = "d994da485d3e1821bcf20a0da5f38ee3d28bd8c0f00389a59adb7ce1196dbce7",
        urls = ["https://github.com/google/quiche/archive/{version}.tar.gz"],
        strip_prefix = "quiche-{version}",
    ),
)
