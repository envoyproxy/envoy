load("@rules_python//python:defs.bzl", "py_binary")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

exports_files(["protodoc_manifest.yaml"])

envoy_package()

py_binary(
    # TODO(phlax): replace this with a clutter index job
    #   and make the clutter binary available to the sphinx job
    name = "copy_clutter_bin_to_path",
    srcs = ["copy_clutter_bin_to_path.py"],
    args = ["$(location @com_github_cluttercode_clutter//:clutter)"],
    data = [
        "@com_github_cluttercode_clutter//:clutter",
    ],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "configs",
    srcs = glob(
        [
            "root/**/*.yaml",
        ],
        exclude = [
            "root/**/envoy-dynamic*.yaml",
            # TODO(phlax/windows-dev): figure out how to get this working on windows
            #      "Error: unable to read file: /etc/ssl/certs/ca-certificates.crt"
            "root/configuration/http/http_filters/_include/dns-cache-circuit-breaker.yaml",
            "root/intro/arch_overview/security/_include/ssl.yaml",
        ],
    ) + select({
        "//bazel:windows_x86_64": [],
        "//conditions:default": [
            "root/configuration/http/http_filters/_include/dns-cache-circuit-breaker.yaml",
            "root/intro/arch_overview/security/_include/ssl.yaml",
        ],
    }),
)

filegroup(
    name = "proto_examples",
    srcs = glob(
        [
            "root/**/*.pb",
        ],
    ),
)
