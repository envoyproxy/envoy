load("@base_pip3//:requirements.bzl", "requirement")
load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")
load("@envoy_repo//:path.bzl", "PATH")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_entry_point", "envoy_genjson", "envoy_pytool_binary")
load("//tools/python:namespace.bzl", "envoy_py_namespace")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_namespace()

bool_flag(
    name = "preload_cve_data",
    build_setting_default = False,
)

config_setting(
    name = "preloaded_cve_data",
    flag_values = {
        ":preload_cve_data": "true",
    },
)

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

envoy_entry_point(
    name = "check",
    args = [
        "--repository_locations=$(location :filtered-dependencies)",
        "--cve_config=$(location :cve.yaml)",
    ] + select({
        ":preloaded_cve_data": ["--cve_data=$(location :cve_data)"],
        "//conditions:default": [],
    }),
    data = [
        ":cve.yaml",
        ":filtered-dependencies",
    ] + select({
        ":preloaded_cve_data": [":cve_data"],
        "//conditions:default": [],
    }),
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

genrule(
    name = "cve_data",
    outs = ["cve_data.json.tar"],
    cmd = """
    $(location :cve_download) \
        --download_cves $@ \
        --repository_locations=$(location //bazel:all_repository_locations)
    """,
    tools = [
        ":cve_download",
        "//bazel:all_repository_locations",
    ],
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
