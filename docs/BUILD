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
        ["root/**/*.yaml"],
        exclude = [
            # `some.customer.filter`
            "root/intro/_include/life-of-a-request.yaml",
            # "Error: No TLS certificates found for server context"
            "root/intro/arch_overview/security/_include/ssl.yaml",
            # missing proto.pb
            "root/configuration/http/http_filters/_include/grpc-transcoder-filter.yaml",
        ],
    ) + select({
        "//bazel:windows_x86_64": [],
        "//conditions:default": [
            # windows: "Error: unable to read file: /etc/ssl/certs/ca-certificates.crt"
	    "root/configuration/http/http_filters/_include/dns-cache-circuit-breaker.yaml",
        ],
    }),
)
