load("@base_pip3//:requirements.bzl", "requirement")
load("@envoy_repo//:path.bzl", "PATH")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_entry_point", "envoy_genjson", "envoy_pytool_binary")
load("//tools/python:namespace.bzl", "envoy_py_namespace")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_namespace()

# Currently we are unable to check for the libdrdkafka dep
# this is a workaround to just exclude it from checks for now
# which is sub-optimal as it also excludes it from CVE scanning
# https://github.com/envoyproxy/envoy/issues/31394
envoy_genjson(
    name = "filtered-dependencies",
    srcs = ["//bazel:all_repository_locations"],
    filter = """
    .[0]
    | del(.confluentinc_librdkafka)
    """,
)

envoy_genjson(
    name = "cpe-dependencies",
    srcs = [":filtered-dependencies"],
    args = [
        "-s",
        "-c",
        "-L",
        "tools/dependency",
    ],
    data = [":cve_utils.jq"],
    filter = """
    import "cve_utils" as Utils;
    .[0] as $deps
    | Utils::parse_deps($deps)
    """,
    visibility = ["//visibility:public"],
)

envoy_entry_point(
    name = "check",
    args = [
        "--repository_locations=$(location :filtered-dependencies)",
    ],
    data = [
        ":filtered-dependencies",
    ],
    pkg = "envoy.dependency.check",
    deps = [requirement("orjson")],
)

envoy_entry_point(
    name = "dependatool",
    args = ["--path=%s" % PATH],
    pkg = "dependatool",
)

envoy_pytool_binary(
    name = "validate",
    srcs = ["validate.py"],
    args = [
        "$(location //bazel:all_repository_locations)",
        "$(location //source/extensions:extensions_build_config)",
    ],
    data = [
        "//bazel:all_repository_locations",
        "//source/extensions:extensions_build_config",
    ],
    deps = [
        "@envoy_repo",
        requirement("aio.api.bazel"),
    ],
)

py_binary(
    name = "validate_test",
    srcs = ["validate_test.py"],
    deps = [":validate"],
)

envoy_entry_point(
    name = "cve_download",
    pkg = "envoy.dependency.check",
    deps = [requirement("orjson")],
)

envoy_genjson(
    name = "build-images",
    filter = """
    .[0]["build-image"]
    """,
    yaml_srcs = ["//:.github/config.yml"],
)

sh_binary(
    name = "build-image-sha",
    srcs = ["version.sh"],
    args = [
        "$(JQ_BIN)",
        "$(location :build-images)",
    ],
    data = [
        ":build-images",
        "@jq_toolchains//:resolved_toolchain",
    ],
    toolchains = ["@jq_toolchains//:resolved_toolchain"],
)
