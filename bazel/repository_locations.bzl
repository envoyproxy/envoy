# This should match the schema defined in external_deps.bzl.

REPOSITORY_LOCATIONS_SPEC = dict(
    quiche = dict(
        version = "474fbc6d05fab1373644829594cc0fe342d3a049",
        sha256 = "ed4651a2e0ebbda204f24a34083dd317b0f9ff4b10f7bb66613697e600a09f5e",
        urls = ["https://github.com/google/quiche/archive/{version}.tar.gz"],
        strip_prefix = "quiche-{version}",
    ),
)
