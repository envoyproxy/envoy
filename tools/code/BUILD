load("@aspect_bazel_lib//lib:jq.bzl", "jq")
load("@envoy_repo//:path.bzl", "PATH")
load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")
load("@rules_python//python/entry_points:py_console_script_binary.bzl", "py_console_script_binary")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load(
    "//test/extensions/filters/network/common/fuzz:config.bzl",
    "READFILTER_FUZZ_FILTERS",
    "READFILTER_NOFUZZ_FILTERS",
)

licenses(["notice"])  # Apache 2

CODE_CHECKS = [
    "changelog",
    "extensions_fuzzed",
    "extensions_metadata",
    "extensions_owners",
    "extensions_registered",
    "glint",
    "gofmt",
    "python_flake8",
    "runtime_guards",
    "shellcheck",
    "yamllint",
]

FUZZ_FILTER_COUNT = (
    len(READFILTER_FUZZ_FILTERS) +
    len(READFILTER_NOFUZZ_FILTERS)
)

jq(
    name = "extensions_build_config",
    srcs = [
        "//contrib:contrib_extensions_build_config",
        "//source/extensions:extensions_build_config",
    ],
    out = "extensions_build_config.json",
    args = ["--slurp"],
    filter = ".[0] * .[1]",
)

py_console_script_binary(
    name = "check",
    args = [
        "--codeowners=$(location //:CODEOWNERS)",
        "--owners=$(location //:OWNERS.md)",
        "--extensions_build_config=$(location :extensions_build_config)",
        "--extensions_fuzzed_count=%s" % FUZZ_FILTER_COUNT,
        "--path=%s" % PATH,
        "-c %s" % " ".join(CODE_CHECKS),
        "-b shellcheck:$(location @shellcheck)",
        "-b gofmt:$(location @go_sdk//:bin/gofmt)",
        "-x bazel/repo.bzl",
    ],
    data = [
        ":extensions_build_config",
        "//:CODEOWNERS",
        "//:OWNERS.md",
        "@go_sdk//:bin/gofmt",
        "@shellcheck",
    ],
    pkg = "@base_pip3//envoy_code_check",
    script = "envoy.code.check",
)

genrule(
    name = "checked",
    outs = ["checked.txt"],
    cmd = """
    $(location :check) \
        --fix \
        -l warn \
        -v warn \
        -x mobile/dist/envoy-pom.xml \
        -x bazel/repo.bzl \
        --codeowners=$(location //:CODEOWNERS) \
        --owners=$(location //:OWNERS.md) \
        --extensions_build_config=$(location :extensions_build_config) \
        --extensions_fuzzed_count=%s \
        --path=%s \
        -c %s \
        -b shellcheck:$(location @shellcheck) \
        -b gofmt:$(location @go_sdk//:bin/gofmt) \
      > $@ 2>&1 || :
    """ % (
        FUZZ_FILTER_COUNT,
        PATH,
        " ".join(CODE_CHECKS),
    ),
    tags = ["no-remote-exec"],
    tools = [
        ":check",
        ":extensions_build_config",
        "//:CODEOWNERS",
        "//:OWNERS.md",
        "//bazel:volatile-scm-hash",
        "@go_sdk//:bin/gofmt",
        "@shellcheck",
    ],
)

sh_test(
    name = "check_test",
    srcs = [":check_test.sh"],
    args = ["$(location :checked)"],
    data = [":checked"],
    tags = ["no-remote-exec"],
)

py_library(
    name = "narrow_test_mocks_lib",
    srcs = ["narrow_test_mocks.py"],
    visibility = ["//tools/code/tests:__pkg__"],
)

py_binary(
    name = "narrow_test_mocks",
    srcs = ["narrow_test_mocks.py"],
    args = [
        "--root=%s" % PATH,
    ],
    main = "narrow_test_mocks.py",
    visibility = ["//visibility:public"],
)

py_test(
    name = "narrow_test_mocks_test",
    srcs = ["tests/narrow_test_mocks_test.py"],
    main = "tests/narrow_test_mocks_test.py",
    deps = [":narrow_test_mocks_lib"],
)
