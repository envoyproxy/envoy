load("@bazel_gazelle//:deps.bzl", "go_repository")
load("@build_bazel_apple_support//lib:repositories.bzl", "apple_support_dependencies")
load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")
load("@build_bazel_rules_swift//swift:repositories.bzl", "swift_rules_dependencies")
load("@robolectric//bazel:robolectric.bzl", "robolectric_repositories")
load("@rules_detekt//detekt:dependencies.bzl", "rules_detekt_dependencies")
load("@rules_java//java:repositories.bzl", "rules_java_dependencies")
load("@rules_jvm_external//:defs.bzl", "maven_install")
load("@rules_kotlin//kotlin:repositories.bzl", "kotlin_repositories")
load("@rules_proto//proto:repositories.bzl", "rules_proto_dependencies")
load("@rules_proto//proto:toolchains.bzl", "rules_proto_toolchains")
load("@rules_proto_grpc//:repositories.bzl", "rules_proto_grpc_repos", "rules_proto_grpc_toolchains")
load("@rules_python//python:pip.bzl", "pip_parse")
load("@rules_shell//shell:repositories.bzl", "rules_shell_dependencies", "rules_shell_toolchains")

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
    python_dependencies()

def swift_dependencies():
    apple_support_dependencies()
    apple_rules_dependencies(ignore_version_differences = True)
    swift_rules_dependencies()

def kotlin_dependencies(extra_maven_dependencies = []):
    rules_java_dependencies()
    maven_install(
        name = "rules_android_maven",
        artifacts = [
            "androidx.privacysandbox.tools:tools:1.0.0-alpha06",
            "androidx.privacysandbox.tools:tools-apigenerator:1.0.0-alpha06",
            "androidx.privacysandbox.tools:tools-apipackager:1.0.0-alpha06",
            "androidx.test:core:1.6.0-alpha01",
            "androidx.test.ext:junit:1.2.0-alpha01",
            "com.android.tools.apkdeployer:apkdeployer:8.11.0-alpha10",
            "com.android.tools.build:bundletool:1.18.2",
            "com.android.tools:desugar_jdk_libs_minimal:2.1.5",
            "com.android.tools:desugar_jdk_libs_configuration_minimal:2.1.5",
            "com.android.tools:desugar_jdk_libs_nio:2.1.5",
            "com.android.tools:desugar_jdk_libs_configuration_nio:2.1.5",
            "com.android.tools:desugar_jdk_libs_configuration:2.1.5",
            "com.android.tools:r8:8.9.35",
            "org.bouncycastle:bcprov-jdk18on:1.77",
            "org.hamcrest:hamcrest-core:2.2",
            "org.robolectric:robolectric:4.14.1",
            "com.google.flogger:flogger:0.8",
            "com.google.flogger:flogger-system-backend:0.8",
            "com.google.guava:guava:32.1.2-jre",
            "com.google.guava:failureaccess:1.0.1",
            "info.picocli:picocli:4.7.4",
            "jakarta.inject:jakarta.inject-api:2.0.1",
            "junit:junit:4.13.2",
            "com.beust:jcommander:1.82",
            "com.google.protobuf:protobuf-java:4.33.4",
            "com.google.protobuf:protobuf-java-util:4.33.4",
            "com.google.code.findbugs:jsr305:3.0.2",
            "androidx.databinding:databinding-compiler:8.7.0",
            "org.ow2.asm:asm:9.6",
            "org.ow2.asm:asm-commons:9.6",
            "org.ow2.asm:asm-tree:9.6",
            "org.ow2.asm:asm-util:9.6",
            "com.android:zipflinger:8.7.0",
            "com.android.tools.build:gradle:8.7.0",
            "com.android:signflinger:8.7.0",
            "com.android.tools.build:aapt2-proto:8.6.1-11315950",
            "com.android.tools.build:apksig:8.7.0",
            "com.android.tools.build:apkzlib:8.7.0",
            "com.google.auto.value:auto-value:1.11.0",
            "com.google.auto.value:auto-value-annotations:1.11.0",
            "com.google.auto:auto-common:1.2.2",
            "com.google.auto.service:auto-service:1.1.1",
            "com.google.auto.service:auto-service-annotations:1.1.1",
            "com.google.errorprone:error_prone_annotations:2.33.0",
            "com.google.errorprone:error_prone_type_annotations:2.33.0",
            "com.google.errorprone:error_prone_check_api:2.33.0",
            "com.google.errorprone:error_prone_core:2.33.0",
            "org.conscrypt:conscrypt-openjdk-uber:2.5.2",
        ],
        repositories = [
            "https://repo1.maven.org/maven2",
            "https://maven.google.com",
        ],
        use_starlark_android_rules = True,
        aar_import_bzl_label = "@rules_android//rules:rules.bzl",
    )
    maven_install(
        name = "android_ide_common_30_1_3",
        aar_import_bzl_label = "@rules_android//rules:rules.bzl",
        artifacts = [
            "com.android.tools.layoutlib:layoutlib-api:30.1.3",
            "com.android.tools.build:manifest-merger:30.1.3",
            "com.android.tools:common:30.1.3",
            "com.android.tools:repository:30.1.3",
            "com.android.tools.analytics-library:protos:30.1.3",
            "com.android.tools.analytics-library:shared:30.1.3",
            "com.android.tools.analytics-library:tracker:30.1.3",
            "com.android.tools:annotations:30.1.3",
            "com.android.tools:sdk-common:30.1.3",
            "com.android.tools.build:builder:7.1.3",
            "com.android.tools.build:builder-model:7.1.3",
            "com.google.protobuf:protobuf-java:4.33.4",
            "com.google.protobuf:protobuf-java-util:4.33.4",
        ],
        repositories = [
            "https://maven.google.com",
            "https://repo1.maven.org/maven2",
        ],
        use_starlark_android_rules = True,
    )
    maven_install(
        name = "bazel_worker_maven",
        artifacts = [
            "com.google.code.gson:gson:2.10.1",
            "com.google.errorprone:error_prone_annotations:2.23.0",
            "com.google.guava:guava:33.0.0-jre",
            "com.google.protobuf:protobuf-java:4.33.4",
            "com.google.protobuf:protobuf-java-util:4.33.4",
            "junit:junit:4.13.2",
            "org.mockito:mockito-core:5.4.0",
            "com.google.truth:truth:1.4.0",
        ],
        aar_import_bzl_label = "@rules_android//rules:rules.bzl",
        repositories = [
            "https://repo1.maven.org/maven2",
            "https://maven.google.com",
        ],
    )
    maven_install(
        artifacts = [
            "com.google.code.findbugs:jsr305:3.0.2",
            "androidx.annotation:annotation:1.5.0",
            # Java Proto Lite
            "com.google.protobuf:protobuf-javalite:4.33.1",
            # Kotlin
            "org.jetbrains:annotations:23.0.0",
            "org.jetbrains.kotlin:kotlin-stdlib-jdk8:1.9.23",
            "org.jetbrains.kotlin:kotlin-stdlib-common:1.9.23",
            "org.jetbrains.kotlin:kotlin-stdlib:1.9.23",
            "androidx.recyclerview:recyclerview:1.1.0",
            "androidx.core:core:1.3.2",
            # Dokka
            "org.jetbrains.dokka:dokka-cli:1.5.31",
            "org.jetbrains.dokka:javadoc-plugin:1.5.31",
            # Test artifacts
            "com.google.truth:truth:1.4.4",
            "junit:junit:4.13.2",
            "org.mockito:mockito-inline:5.2.0",
            "org.mockito:mockito-core:5.14.2",
            "com.squareup.okhttp3:okhttp:4.12.0",
            "com.squareup.okhttp3:mockwebserver:4.12.0",
            "io.netty:netty-all:4.1.115.Final",
            # Android test artifacts
            "androidx.test:core:1.5.0",
            "androidx.test:rules:1.5.0",
            "androidx.test:runner:1.5.0",
            "androidx.test:monitor:1.5.0",
            "androidx.test.ext:junit:1.1.5",
            "org.robolectric:robolectric:4.16",
            "org.hamcrest:hamcrest:3.0",
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

    go_repository(
        name = "com_github_google_go_cmp",
        importpath = "github.com/google/go-cmp",
        sum = "h1:O2Tfq5qg4qc4AmwVlvv0oLiVAGB7enBSJ2x2DqQFi38=",
        version = "v0.5.9",
    )
    go_repository(
        name = "org_golang_x_sync",
        importpath = "golang.org/x/sync",
        sum = "h1:5KslGYwFpkhGh+Q16bwMP3cOontH8FOep7tGV86Y7SQ=",
        version = "v0.0.0-20210220032951-036812b2e83c",
    )
    go_repository(
        name = "com_github_golang_glog",
        importpath = "github.com/golang/glog",
        version = "v1.1.2",
        sum = "h1:DVjP2PbBOzHyzA+dn3WhHIq4NdVu3Q+pvivFICf/7fo=",
    )
    go_repository(
        name = "org_bitbucket_creachadair_stringset",
        importpath = "bitbucket.org/creachadair/stringset",
        version = "v0.0.14",
        sum = "h1:t1ejQyf8utS4GZV/4fM+1gvYucggZkfhb+tMobDxYOE=",
    )

    rules_shell_dependencies()
    rules_shell_toolchains()

def python_dependencies():
    pip_parse(
        name = "mobile_pip3",
        python_interpreter_target = "@python3_12_host//:python",
        requirements_lock = "//tools/python:requirements.txt",
        timeout = 1000,
    )
