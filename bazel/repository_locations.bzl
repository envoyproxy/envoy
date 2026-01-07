# This should match the schema defined in external_deps.bzl.

REPOSITORY_LOCATIONS_SPEC = dict(
    quiche = dict(
        version = "b7b4c0cfe393a57b8706b0f1be81518595daaa44",
        sha256 = "9d8344faf932165b6013f8fdd2cbfe2be7c2e7a5129c5e572036d13718a3f1bf",
        urls = ["https://github.com/google/quiche/archive/{version}.tar.gz"],
        strip_prefix = "quiche-{version}",
    ),
)
