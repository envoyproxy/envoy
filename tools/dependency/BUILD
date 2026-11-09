load("@base_pip3//:requirements.bzl", "requirement")
load("@envoy_repo//:path.bzl", "PATH")
load("@envoy_toolshed//:utils.bzl", "json_merge")
load("@envoy_toolshed//dependency/cve:macros.bzl", "cve_scan")
load("@rules_python//python:defs.bzl", "py_binary")
load("@rules_python//python/entry_points:py_console_script_binary.bzl", "py_console_script_binary")
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")

licenses(["notice"])  # Apache 2

# Currently we are unable to check for the libdrdkafka dep
# this is a workaround to just exclude it from checks for now
# which is sub-optimal as it also excludes it from CVE scanning
# https://github.com/envoyproxy/envoy/issues/31394
json_merge(
    name = "filtered-dependencies",
    srcs = ["//bazel:all_repository_locations"],
    filter = """
    .[0]
    | del(.confluentinc_librdkafka)
    """,
    visibility = ["//visibility:public"],
)

py_console_script_binary(
    name = "check",
    args = [
        "--repository_locations=$(location :filtered-dependencies)",
    ],
    data = [
        ":filtered-dependencies",
    ],
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

py_binary(
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

json_merge(
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

filegroup(
    name = "cve-data-dir",
    srcs = glob([
        "cve_data/*.json",
        "cve_data/cvelist/*.json",
    ]),
    visibility = ["//visibility:public"],
)

genrule(
    name = "placeholder",
    outs = ["PLACEHOLDER.txt"],
    cmd = "echo '' > $@",
)

filegroup(
    name = "empty-directory",
    srcs = [":placeholder"],  # default fallback
    visibility = ["//visibility:public"],
)

label_flag(
    name = "cve-data",
    build_setting_default = ":empty-directory",
    visibility = ["//visibility:public"],
)

cve_scan(
    name = "cves",
    config = ":cve.yaml",
    cve_data = ":cve-data",
    visibility = ["//visibility:public"],
    deps = ":filtered-dependencies",
)
