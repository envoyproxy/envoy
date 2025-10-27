load("@base_pip3//:requirements.bzl", "requirement")
load("@envoy_repo//:path.bzl", "PATH")
load("@rules_python//python:defs.bzl", "py_binary")
load("@rules_python//python/entry_points:py_console_script_binary.bzl", "py_console_script_binary")
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools/base:envoy_python.bzl", "envoy_genjson")

licenses(["notice"])  # Apache 2

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
    data = [
        ":cve_utils.jq",
        ":version.jq",
    ],
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

envoy_genjson(
    name = "start-year",
    filter = """
    .[0].start_year
    """,
    visibility = ["//visibility:public"],
    yaml_srcs = [":cve.yaml"],
)

py_binary(
    name = "cve_fetch",
    srcs = ["cve_fetch.py"],
    deps = [
        requirement("aiohttp"),
        requirement("aio.run.runner"),
    ],
)

sh_binary(
    name = "cve_update",
    srcs = ["cve_update.sh"],
    data = [
        ":cve_fetch",
        ":start-year",
    ],
    env = {
        "FETCHER": "$(location :cve_fetch)",
        "START_YEAR_PATH": "$(location :start-year)",
        # NOTE: usage of PATH means this _must_ be run locally
        "CVE_DATA_PATH": "%s/tools/dependency/cve_data" % PATH,
    },
)

envoy_genjson(
    name = "ignored-cves",
    filter = """
    .[0].ignored_cves
    """,
    visibility = ["//visibility:public"],
    yaml_srcs = [":cve.yaml"],
)

filegroup(
    name = "cve-data-dir",
    srcs = glob(["cve_data/*.json"]),
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

sh_binary(
    name = "cves",
    srcs = ["cves.sh"],
    data = [
        ":cpe-dependencies",
        ":cve-data",
        ":cve_matcher.jq",
        ":cve_utils.jq",
        ":ignored-cves.json",
        ":version.jq",
        "@jq_toolchains//:resolved_toolchain",
    ],
    env = {
        "JQ_BIN": "$(JQ_BIN)",
        "CPE_DEPS": "$(location :cpe-dependencies)",
        "JQ_CVE_UTILS": "$(location :cve_utils.jq)",
        "JQ_CVE_MATCHER": "$(location :cve_matcher.jq)",
        "JQ_VERSION_UTILS": "$(location :version.jq)",
        "CVES_IGNORED": "$(location :ignored-cves.json)",
        "CVES": "$(locations :cve-data)",
    },
    toolchains = ["@jq_toolchains//:resolved_toolchain"],
)

genrule(
    name = "cves-scanned",
    outs = ["scanned.json"],
    cmd = """
    export JQ_BIN="$(JQ_BIN)"
    export CPE_DEPS="$(location :cpe-dependencies)"
    export JQ_CVE_UTILS="$(location :cve_utils.jq)"
    export JQ_CVE_MATCHER="$(location :cve_matcher.jq)"
    export JQ_VERSION_UTILS="$(location :version.jq)"
    export CVES_IGNORED="$(location :ignored-cves.json)"
    export CVES="$(locations :cve-data)"
    read -ra CVELIST <<< "$$CVES"
    HAS_JSON=false
    for f in "$${CVELIST[@]}"; do
        if [[ "$$f" == *.json ]]; then
            HAS_JSON=true
            break
        fi
    done
    if [[ "$$HAS_JSON" != true ]]; then
        echo "No CVE data set, perhaps use --config=cves?" >&2
        exit 1
    fi
    $(location :cves) \
      > $@ || :
    """,
    tags = ["no-remote-exec"],
    toolchains = ["@jq_toolchains//:resolved_toolchain"],
    tools = [
        ":cpe-dependencies",
        ":cve-data",
        ":cve_matcher.jq",
        ":cve_utils.jq",
        ":cves",
        ":ignored-cves.json",
        ":version.jq",
        "@jq_toolchains//:resolved_toolchain",
    ],
)

sh_test(
    name = "cve_test",
    srcs = [":cve_test.sh"],
    args = ["$(location :cves-scanned)"],
    data = [
        ":ansi.jq",
        ":cve_report.jq",
        ":cve_utils.jq",
        ":cves-scanned",
        ":version.jq",
        "@jq_toolchains//:resolved_toolchain",
    ],
    env = {
        "JQ_BIN": "$(JQ_BIN)",
        "JQ_ANSI_UTILS": "$(location :ansi.jq)",
        "JQ_CVE_UTILS": "$(location :cve_utils.jq)",
        "JQ_VERSION_UTILS": "$(location :version.jq)",
        "JQ_REPORT": "$(location :cve_report.jq)",
    },
    tags = ["no-remote-exec"],
    toolchains = ["@jq_toolchains//:resolved_toolchain"],
)
