load("@base_pip3//:requirements.bzl", "requirement")
load("@bazel_skylib//rules:build_test.bzl", "build_test")
load("@envoy_repo//:path.bzl", "PATH")
load("@envoy_toolshed//:utils.bzl", "json_merge")
load("@envoy_toolshed//dependency:reachability.bzl", "dependency_reachability")
load("@envoy_toolshed//dependency/cve:macros.bzl", "cve_scan")
load("@rules_python//python:defs.bzl", "py_test")
load("@rules_python//python/entry_points:py_console_script_binary.bzl", "py_console_script_binary")
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load(":reachability.bzl", "WASM_RUNTIME_FLAG", "envoy_dependency_reachability")

licenses(["notice"])  # Apache 2

# This does a unicode sort
DEPS_ORDER_FILTER = """
.[0]
| keys_unsorted as $actual
| keys as $expected
| if $actual == $expected then {sorted: true}
  else
    ([$actual, $expected]
     | transpose
     | map(select(.[0] != .[1]))
     | first) as $mismatch
    | error(
        "deps metadata is not sorted alphabetically, "
        + "expected \\($mismatch[1]) where \\($mismatch[0]) is")
  end
"""

# Currently we are unable to check for the libdrdkafka dep
# this is a workaround to just exclude it from checks for now
# which is sub-optimal as it also excludes it from CVE scanning
# https://github.com/envoyproxy/envoy/issues/31394
json_merge(
    name = "filtered-dependencies",
    srcs = ["//bazel:all_repository_locations"],
    filter = """
    .[0]
    | del(.librdkafka)
    | del(.["antlr4-cpp-runtime"])
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

# Core-only root: used to compute the "core" dep set (deps of non-extension
# production code).  Extension-marginal deps are computed as
# (all-extensions reachable) minus (core reachable).
#
# Single-config: the core dep set does not vary with the wasm runtime, so
# there is nothing for a matrix to add here.
#
# Intentionally no excluded_patterns here: core_deps is only used to skip the
# forward extension-category assertion, so a larger core set is only more
# lenient and cannot introduce false validation failures.
dependency_reachability(
    name = "dep-reachability-core",
    roots = ["//source/exe:envoy_main_common_with_core_extensions_lib"],
)

# All-extensions root: traverses every extension so that validate_extension_deps
# actually observes non-core extension deps.  This is the primary reachability
# data used by all checks.
#
# The wasm runtimes are mutually exclusive within a single configuration: only
# one of v8/wamr/wasmtime is built at a time.  Analyzing a single config would
# therefore make it impossible for eg envoy.wasm.runtime.wamr and
# envoy.wasm.runtime.wasmtime to both be attributed, and deps legitimately
# listing both (proxy_wasm_cpp_host, proxy_wasm_cpp_sdk, rules_rust) would be
# reported as carrying stale `extensions:` entries.
#
# Data from all analyzed configs is merged by union, so the emitted JSON is
# config-independent and `deps.yaml` metadata does not have to be written
# against whichever config CI happens to run.
#
# See :reachability.bzl for why the rule is constructed there rather than here.
envoy_dependency_reachability(
    name = "dep-reachability",
    attribution_patterns = [
        "//source/extensions/...",
    ],
    configs = {
        "v8": {WASM_RUNTIME_FLAG: "v8"},
        "wamr": {WASM_RUNTIME_FLAG: "wamr"},
        "wasmtime": {WASM_RUNTIME_FLAG: "wasmtime"},
    },
    excluded_patterns = [
        "base_pip3_*",
        "bazel_tools",
        "cu__*",
        "envoy_rust_crate_*",
        "envoy_api",
        "sysroot_linux_*",
        "v8_python_*",
    ],
    roots = ["//source/exe:main_common_with_all_extensions_lib"],
)

py_test(
    name = "validate_reachability_test",
    srcs = ["validate_reachability_test.py"],
    data = [
        ":dep-reachability",
        ":dep-reachability-core",
        "//bazel:all_repository_locations",
        "//source/extensions:extensions_build_config",
    ],
    env = {
        "REACHABILITY_JSON": "$(location :dep-reachability)",
        "CORE_REACHABILITY_JSON": "$(location :dep-reachability-core)",
        "REPOSITORY_LOCATIONS_JSON": "$(location //bazel:all_repository_locations)",
        "EXTENSIONS_BUILD_CONFIG_JSON": "$(location //source/extensions:extensions_build_config)",
    },
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

cve_scan(
    name = "cves",
    config = ":cve.yaml",
    cve_data = ":cve-data",
    visibility = ["//visibility:public"],
    deps = ":filtered-dependencies",
)

# These targets are for testing
json_merge(
    name = "deps-order",
    filter = DEPS_ORDER_FILTER,
    yaml_srcs = ["//bazel:deps.yaml"],
)

json_merge(
    name = "api-deps-order",
    filter = DEPS_ORDER_FILTER,
    yaml_srcs = ["@envoy_api//bazel:deps.yaml"],
)

build_test(
    name = "deps_order_test",
    targets = [
        ":deps-order",
        ":api-deps-order",
    ],
)
