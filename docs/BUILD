load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

exports_files(["protodoc_manifest.yaml"])

envoy_package()

# dns-cache-circuit-breaker fails on windows: "Error: unable to read file: /etc/ssl/certs/ca-certificates.crt"

filegroup(
    name = "configs",
    srcs = glob(
        ["root/**/*.yaml"],
        exclude = [
            "root/intro/_include/life-of-a-request.yaml",
            "root/intro/arch_overview/security/_include/ssl.yaml",
            "root/configuration/http/http_filters/_include/dns-cache-circuit-breaker.yaml",
            "root/configuration/http/http_filters/_include/grpc-reverse-bridge-filter.yaml",
            "root/configuration/http/http_filters/_include/grpc-transcoder-filter.yaml",
        ],
    ),
)
