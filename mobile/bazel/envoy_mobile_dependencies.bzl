load("@build_bazel_apple_support//lib:repositories.bzl", "apple_support_dependencies")
load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")
load("@build_bazel_rules_swift//swift:repositories.bzl", "swift_rules_dependencies")
load("@io_bazel_rules_kotlin//kotlin:repositories.bzl", "kotlin_repositories")
load("@robolectric//bazel:robolectric.bzl", "robolectric_repositories")
load("@rules_detekt//detekt:dependencies.bzl", "rules_detekt_dependencies")
load("@rules_java//java:repositories.bzl", "rules_java_dependencies")
load("@rules_jvm_external//:defs.bzl", "maven_install")
load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies")
load("@rules_proto//proto:toolchains.bzl", "rules_proto_toolchains")
load("@rules_proto_grpc//:repositories.bzl", "rules_proto_grpc_repos", "rules_proto_grpc_toolchains")

def _default_extra_swift_sources_impl(ctx):
    ctx.file("WORKSPACE", "")
    ctx.file("empty.swift", "")
    ctx.file("BUILD.bazel", """
filegroup(
    name = "extra_swift_srcs",
    srcs = ["empty.swift"],
    visibility = ["//visibility:public"],
)

objc_library(
    name = "extra_private_dep",
    module_name = "FakeDep",
    visibility = ["//visibility:public"],
)""")

_default_extra_swift_sources = repository_rule(
    implementation = _default_extra_swift_sources_impl,
)

def _default_extra_jni_deps_impl(ctx):
    ctx.file("WORKSPACE", "")
    ctx.file("BUILD.bazel", """
cc_library(
    name = "extra_jni_dep",
    visibility = ["//visibility:public"],
)""")

_default_extra_jni_deps = repository_rule(
    implementation = _default_extra_jni_deps_impl,
)

def envoy_mobile_dependencies(extra_maven_dependencies = []):
    if not native.existing_rule("envoy_mobile_extra_swift_sources"):
        _default_extra_swift_sources(name = "envoy_mobile_extra_swift_sources")
    if not native.existing_rule("envoy_mobile_extra_jni_deps"):
        _default_extra_jni_deps(name = "envoy_mobile_extra_jni_deps")

    swift_dependencies()
    kotlin_dependencies(extra_maven_dependencies)

def swift_dependencies():
    apple_support_dependencies()
    apple_rules_dependencies(ignore_version_differences = True)
    swift_rules_dependencies()

def kotlin_dependencies(extra_maven_dependencies = []):
    rules_java_dependencies()
    maven_install(
        artifacts = [
            "com.google.code.findbugs:jsr305:3.0.2",
            # Java Proto Lite
            "com.google.protobuf:protobuf-javalite:3.24.4",
            # Kotlin
            "org.jetbrains.kotlin:kotlin-stdlib-jdk8:1.6.21",
            "org.jetbrains.kotlin:kotlin-stdlib-common:1.6.21",
            "org.jetbrains.kotlin:kotlin-stdlib:1.6.21",
            "androidx.recyclerview:recyclerview:1.1.0",
            "androidx.core:core:1.3.2",
            # Dokka
            "org.jetbrains.dokka:dokka-cli:1.5.31",
            "org.jetbrains.dokka:javadoc-plugin:1.5.31",
            # Test artifacts
            "com.google.truth:truth:1.4.0",
            "junit:junit:4.13",
            "org.mockito:mockito-inline:4.8.0",
            "org.mockito:mockito-core:4.8.0",
            "com.squareup.okhttp3:okhttp:4.10.0",
            "com.squareup.okhttp3:mockwebserver:4.10.0",
            "io.netty:netty-all:4.1.82.Final",
            # Android test artifacts
            "androidx.test:core:1.4.0",
            "androidx.test:rules:1.4.0",
            "androidx.test:runner:1.4.0",
            "androidx.test:monitor:1.5.0",
            "androidx.test.ext:junit:1.1.3",
            "org.robolectric:robolectric:4.8.2",
            "org.hamcrest:hamcrest:2.2",
        ] + extra_maven_dependencies,
        version_conflict_policy = "pinned",
        repositories = [
            "https://repo1.maven.org/maven2",
            "https://maven.google.com",
        ],
    )
    kotlin_repositories()
    rules_detekt_dependencies()
    robolectric_repositories()

    rules_proto_grpc_toolchains()
    rules_proto_grpc_repos()
    rules_proto_dependencies()
    rules_proto_toolchains()
