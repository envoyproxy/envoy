load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# Copies of certain lighweight testing Maxmind databases are included into this filegroup:
# https://github.com/maxmind/MaxMind-DB/tree/main/test-data
filegroup(
    name = "geolocation_databases",
    srcs = glob(["*.mmdb"]),
)
