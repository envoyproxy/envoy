load("@rules_pkg//pkg:mappings.bzl", "pkg_files")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

filegroup(
    name = "configs",
    srcs = glob(
        [
            "**/*.yaml",
        ],
        exclude = [
            "cache/ci-responses.yaml",
            "cache/responses.yaml",
            "dynamic-config-fs/**/*",
            "jaeger-native-tracing/*",
            "opentelemetry/otel-collector-config.yaml",
            "**/*docker-compose*.yaml",
            # Contrib extensions tested over in contrib.
            "golang-http/*.yaml",
            "golang-network/*.yaml",
            "mysql/*",
            "postgres/*",
            "kafka/*.yaml",
        ],
    ),
)

filegroup(
    name = "contrib_configs",
    srcs = glob(
        [
            "golang-http/*.yaml",
            "golang-network/*.yaml",
            "mysql/*.yaml",
            "postgres/*.yaml",
            "kafka/*.yaml",
        ],
        exclude = [
            "**/*docker-compose*.yaml",
        ],
    ),
)

filegroup(
    name = "certs",
    srcs = glob(["_extra_certs/*.pem"]),
)

filegroup(
    name = "docs_rst",
    srcs = glob(["**/example.rst"]) + ["//examples/wasm-cc:example.rst"],
)

pkg_files(
    name = "examples_files",
    srcs = [":files"],
    prefix = "_include",
    strip_prefix = "/examples",
)

genrule(
    name = "examples_docs",
    srcs = [":docs_rst"],
    outs = ["examples_docs.tar.gz"],
    cmd = """
    TEMP=$$(mktemp -d)
    for location in $(locations :docs_rst); do
        example=$$(echo $$location | cut -d/ -f2)
        cp -a $$location "$${TEMP}/$${example}.rst"
        echo "    $${example}" >> "$${TEMP}/_toctree.rst"
    done
    echo ".. toctree::" > "$${TEMP}/toctree.rst"
    echo "    :maxdepth: 1" >> "$${TEMP}/toctree.rst"
    echo "" >> "$${TEMP}/toctree.rst"
    cat "$${TEMP}/_toctree.rst" | sort >> "$${TEMP}/toctree.rst"
    rm "$${TEMP}/_toctree.rst"
    tar czf $@ -C $${TEMP} .
    """,
)

filegroup(
    name = "lua",
    srcs = glob(["**/*.lua"]),
)

filegroup(
    name = "files",
    srcs = glob(
        [
            "**/*",
        ],
        exclude = [
            "**/node_modules/**",
            "**/*.rst",
        ],
    ) + [
        "//examples/wasm-cc:files",
    ],
)

pkg_tar(
    name = "docs",
    srcs = [":examples_files"],
    extension = "tar.gz",
    package_dir = "start/sandboxes",
    deps = [":examples_docs"],
)
