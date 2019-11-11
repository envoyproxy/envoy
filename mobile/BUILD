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

alias(
    name = "android_javadocs",
    actual = "//library:javadocs",
)

alias(
    name = "android_sources",
    actual = "//library:sources_jar",
)

genrule(
    name = "android_dist",
    srcs = [
        "android_aar",
    ],
    outs = ["stub_android_dist_output"],
    cmd = """
cp $(location :android_aar) dist/envoy.aar
chmod 755 dist/envoy.aar
touch $@
""",
    stamp = True,
)

genrule(
    name = "android_deploy",
    srcs = [
        "android_aar",
        "android_pom",
        "android_javadocs",
        "android_sources",
    ],
    outs = ["stub_android_deploy_output"],
    cmd = """
cp $(location :android_aar) dist/envoy.aar
cp $(location :android_pom) dist/envoy-pom.xml
cp $(location :android_javadocs) dist/envoy-javadoc.jar
cp $(location :android_sources) dist/envoy-sources.jar
chmod 755 dist/envoy.aar
chmod 755 dist/envoy-pom.xml
chmod 755 dist/envoy-javadoc.jar
chmod 755 dist/envoy-sources.jar
orig_dir=$$PWD
pushd dist
zip -r envoy_aar_sources.zip envoy.aar envoy-pom.xml envoy-javadoc.jar envoy-sources.jar > /dev/null
popd
touch $@
""",
    stamp = True,
)

define_kt_toolchain(
    name = "kotlin_toolchain",
    jvm_target = "1.8",
)
