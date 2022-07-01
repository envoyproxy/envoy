load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

filegroup(
    name = "configs",
    srcs = glob(
        [
            "**/*.yaml",
        ],
        exclude = [
            "cache/responses.yaml",
            "dynamic-config-fs/**/*",
            "jaeger-native-tracing/*",
            "**/*docker-compose*.yaml",
            # Contrib extensions tested over in contrib.
            "mysql/*",
            "postgres/*",
        ],
    ),
)

genrule(
    name = "contrib_configs",
    srcs = glob(
        [
            "mysql/*.yaml",
            "postgres/*.yaml",
        ],
        exclude = [
            "**/*docker-compose*.yaml",
        ],
    ),
    outs = ["example_configs.tar"],
    cmd = (
        "$(location //configs:configgen.sh) NO_CONFIGGEN $(@D) $(SRCS)"
    ),
    tools = [
        "//configs:configgen.sh",
    ],
)

filegroup(
    name = "certs",
    srcs = glob(["_extra_certs/*.pem"]),
)

filegroup(
    name = "lua",
    srcs = glob(["**/*.lua"]),
)

filegroup(
    name = "files",
    srcs = glob(["**/*"]) + [
        "//examples/wasm-cc:files",
    ],
)

filegroup(
    name = "shared",
    srcs = glob(["shared/**/*"]),
)

sh_library(
    name = "verify-common",
    srcs = ["verify-common.sh"],
)

pkg_tar(
    name = "verification",
    srcs = [
        "//examples/brotli:verification",
        "//examples/cache:verification",
        "//examples/cors:verification",
        "//examples/csrf:verification",
        # this requires `nc` in path
        # "//examples/dynamic-config-cp:verification",
        "//examples/dynamic-config-fs:verification",
        "//examples/ext_authz:verification",
        # non-shared fs
        # "//examples/fault-injection:verification",
        "//examples/front-proxy:verification",
        "//examples/grpc-bridge:verification",
        "//examples/gzip:verification",
        "//examples/jaeger-native-tracing:verification",
        "//examples/jaeger-tracing:verification",
        "//examples/load-reporting-service:verification",
        "//examples/local_ratelimit:verification",
        "//examples/locality-load-balancing:verification",
        "//examples/lua:verification",
        "//examples/mysql:verification",
        "//examples/postgres:verification",
        # needs client
        # "//examples/redis:verification",
        "//examples/skywalking-tracing:verification",
        "//examples/tls:verification",
        "//examples/tls-sni:verification",
        "//examples/tls-inspector:verification",
        # requires `nc`
        # "//examples/udp:verification",
        # sandbox problem with .rnd file in ~
        # "//examples/websocket:verification",
        "//examples/zipkin-tracing:verification",
        "//examples/zstd:verification",
    ],
)
