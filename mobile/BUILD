load("@io_bazel_rules_kotlin//kotlin/internal:toolchains.bzl", "define_kt_toolchain")

licenses(["notice"])  # Apache 2

alias(
    name = "ios_framework",
    actual = "//library/swift/src:ios_framework",
    visibility = ["//visibility:public"],
)

genrule(
    name = "ios_dist",
    srcs = [":ios_framework"],
    outs = ["ios_out"],
    cmd = """
unzip -o $< -d dist/
touch $@
""",
    stamp = True,
)

alias(
    name = "android_aar",
    actual = "//library/kotlin/src/io/envoyproxy/envoymobile:envoy_aar",
    visibility = ["//visibility:public"],
)

genrule(
    name = "android_dist_ci",
    srcs = [
        "//library/kotlin/src/io/envoyproxy/envoymobile:envoy_aar_with_artifacts",
    ],
    outs = ["envoy_mobile.zip"],
    cmd = """
    for artifact in $(SRCS); do
        chmod 755 $$artifact
        cp $$artifact dist/
    done
    touch $@
    """,
    stamp = True,
    tools = ["//bazel:zipper"],
    visibility = ["//visibility:public"],
)

genrule(
    name = "android_dist",
    srcs = [
        "//library/kotlin/src/io/envoyproxy/envoymobile:envoy_aar",
        "//library/kotlin/src/io/envoyproxy/envoymobile:envoy_aar_pom_xml",
    ],
    outs = ["output_in_dist_directory"],
    cmd = """
    set -- $(SRCS)
    chmod 755 $$1
    chmod 755 $$2
    cp $$1 dist/envoy.aar
    cp $$2 dist/envoy-pom.xml
    touch $@
    """,
    stamp = True,
)

define_kt_toolchain(
    name = "kotlin_toolchain",
    jvm_target = "1.8",
)

filegroup(
    name = "kotlin_lint_config",
    srcs = [".kotlinlint.yml"],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "editor_config",
    srcs = [".editorconfig"],
    visibility = ["//visibility:public"],
)

genrule(
    name = "kotlin_format",
    srcs = ["//:editor_config"],
    outs = ["kotlin_format.txt"],
    cmd = """
    $(location @kotlin_formatter//file) --android "**/*.kt" \
        --reporter=plain --reporter=checkstyle,output=$@ \
        --editorconfig=$(location //:editor_config)
    """,
    tools = ["@kotlin_formatter//file"],
)

genrule(
    name = "kotlin_format_fix",
    srcs = ["//:editor_config"],
    outs = ["kotlin_format_fix.txt"],
    cmd = """
    $(location @kotlin_formatter//file) -F --android "**/*.kt" \
        --reporter=plain --reporter=checkstyle,output=$@ \
        --editorconfig=$(location //:editor_config)
    """,
    tools = ["@kotlin_formatter//file"],
)
