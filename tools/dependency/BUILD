load("@base_pip3//:requirements.bzl", "requirement")
load("@envoy_repo//:path.bzl", "PATH")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_entry_point", "envoy_py_namespace", "envoy_pytool_binary")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_namespace()

envoy_entry_point(
    name = "check",
    args = [
        "--repository_locations=$(location //bazel:all_repository_locations)",
        "--cve_config=$(location :cve.yaml)",
        "--cve_data=$(location :cve_data)",
    ],
    data = [
        ":cve.yaml",
        ":cve_data",
        "//bazel:all_repository_locations",
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
