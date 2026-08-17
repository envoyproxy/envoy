# This should match the schema defined in external_deps.bzl.

REPOSITORY_LOCATIONS_SPEC = dict(
    quiche = dict(
        version = "90a1e2218164586d4dc711bb9639a313d95de9df",
        sha256 = "42336ae13c29d687ad9458e230bc56cc7e36f5ffc02ee78541929e6e165c8c90",
        urls = ["https://github.com/google/quiche/archive/{version}.tar.gz"],
        strip_prefix = "quiche-{version}",
    ),
)
