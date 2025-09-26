load("@base_pip3//:requirements.bzl", "requirement")
load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")
load("@envoy_repo//:path.bzl", "PATH")
load("@rules_python//python:defs.bzl", "py_binary")
load("@rules_python//python/entry_points:py_console_script_binary.bzl", "py_console_script_binary")
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("//tools/base:envoy_python.bzl", "envoy_genjson", "envoy_pytool_binary")
load("//tools/python:namespace.bzl", "envoy_py_namespace")

licenses(["notice"])  # Apache 2

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
    visibility = ["//visibility:public"],
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

py_console_script_binary(
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
    pkg = "@base_pip3//envoy_dependency_check",
    script = "envoy.dependency.check",
    visibility = ["//visibility:public"],
    deps = [requirement("orjson")],
)

py_console_script_binary(
    name = "dependatool",
    args = ["--path=%s" % PATH],
    pkg = "@base_pip3//dependatool",
    script = "dependatool",
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

py_console_script_binary(
    name = "cve_download",
    pkg = "@base_pip3//envoy_dependency_check",
    script = "envoy.dependency.check",
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
