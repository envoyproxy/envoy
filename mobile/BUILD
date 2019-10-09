licenses(["notice"])  # Apache 2

load("@io_bazel_rules_kotlin//kotlin/internal:toolchains.bzl", "define_kt_toolchain")

alias(
    name = "ios_framework",
    actual = "//library/swift/src:ios_framework",
)

genrule(
    name = "ios_dist",
    srcs = ["//:ios_framework"],
    outs = ["ios_out"],
    cmd = """
unzip -o $< -d dist/
touch $@
""",
    stamp = True,
)

alias(
    name = "android_pom",
    actual = "//library/kotlin/src/io/envoyproxy/envoymobile:android_aar_pom",
)

alias(
    name = "android_aar",
    actual = "//library/kotlin/src/io/envoyproxy/envoymobile:android_aar",
)

genrule(
    name = "android_dist",
    srcs = [
        "android_aar",
        "android_pom",
    ],
    outs = ["stub_android_dist_output"],
    cmd = """
cp $(location :android_aar) dist/envoy.aar
cp $(location :android_pom) dist/envoy-pom.xml
chmod 755 dist/envoy.aar
chmod 755 dist/envoy-pom.xml
touch $@
""",
    stamp = True,
)

define_kt_toolchain(
    name = "kotlin_toolchain",
    jvm_target = "1.8",
)
