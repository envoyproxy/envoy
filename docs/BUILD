load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

exports_files(["protodoc_manifest.yaml"])

envoy_package()

filegroup(
    name = "configs",
    srcs = glob(
        [
            "root/**/*.yaml",
            "root/**/*.pb",
        ],
        exclude = [
            # `some.customer.filter`
            "root/intro/_include/life-of-a-request.yaml",
            # windows: "Error: unable to read file: /etc/ssl/certs/ca-certificates.crt"
            "root/configuration/http/http_filters/_include/dns-cache-circuit-breaker.yaml",
        ],
    ) + select({
        "//bazel:windows_x86_64": [],
        "//conditions:default": [
            "root/configuration/http/http_filters/_include/dns-cache-circuit-breaker.yaml",
        ],
    }),
)
