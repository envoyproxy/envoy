load("@build_bazel_rules_swift//swift:repositories.bzl", "swift_rules_dependencies")
load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")
load("@build_bazel_apple_support//lib:repositories.bzl", "apple_support_dependencies")
load("@rules_jvm_external//:defs.bzl", "maven_install")
load("@rules_detekt//detekt:dependencies.bzl", "rules_detekt_dependencies")
load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kotlin_repositories")
load("@io_grpc_grpc_java//:repositories.bzl", "grpc_java_repositories")
load("@rules_proto_grpc//protobuf:repositories.bzl", "protobuf_repos")
load("@rules_proto_grpc//java:repositories.bzl", rules_proto_grpc_java_repos = "java_repos")
load("@robolectric//bazel:robolectric.bzl", "robolectric_repositories")

def envoy_mobile_dependencies():
    swift_dependencies()
    kotlin_dependencies()

def swift_dependencies():
    apple_support_dependencies()
    apple_rules_dependencies(ignore_version_differences = True)
    swift_rules_dependencies()

def kotlin_dependencies():
    maven_install(
        artifacts = [
            # Kotlin
            "org.jetbrains.kotlin:kotlin-stdlib-jdk8:1.3.11",
            "androidx.recyclerview:recyclerview:1.1.0",
            # Test artifacts
            "org.assertj:assertj-core:3.12.0",
            "junit:junit:4.12",
            "org.mockito:mockito-inline:2.28.2",
            "org.mockito:mockito-core:2.28.2",
            # Android test artifacts
            "androidx.test:core:1.3.0",
            "androidx.test:rules:1.3.0",
            "androidx.test:runner:1.3.0",
            "androidx.test:monitor:1.3.0",
            "androidx.test.ext:junit:1.1.2",
            "org.robolectric:robolectric:4.4",
        ],
        repositories = [
            "https://repo1.maven.org/maven2",
            "https://jcenter.bintray.com/",
            "https://maven.google.com",
        ],
    )
    kotlin_repositories()
    rules_detekt_dependencies()
    robolectric_repositories()

    grpc_java_repositories(
        omit_bazel_skylib = True,
        omit_com_google_protobuf = True,
        omit_com_google_protobuf_javalite = True,
        omit_net_zlib = True,
    )
    protobuf_repos()
    rules_proto_grpc_java_repos()
