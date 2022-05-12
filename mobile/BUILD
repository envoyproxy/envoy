load("@build_bazel_rules_android//android:rules.bzl", "aar_import")
load("@build_bazel_rules_apple//apple:apple.bzl", "apple_static_framework_import")
load("@io_bazel_rules_kotlin//kotlin/internal:toolchains.bzl", "define_kt_toolchain")
load("@com_github_buildbuddy_io_rules_xcodeproj//xcodeproj:xcodeproj.bzl", "xcodeproj")
load("//bazel:framework_imports_extractor.bzl", "framework_imports_extractor")

licenses(["notice"])  # Apache 2

alias(
    name = "ios_xcframework",
    actual = "//library/swift:Envoy",
    visibility = ["//visibility:public"],
)

alias(
    name = "ios_dist",
    actual = "//library/swift:ios_framework",
)

framework_imports_extractor(
    name = "framework_imports",
    framework = "//library/swift:ios_framework",
)

apple_static_framework_import(
    name = "envoy_mobile_ios",
    framework_imports = [":framework_imports"],
    sdk_dylibs = [
        "resolv.9",
        "c++",
    ],
    sdk_frameworks = [
        "Network",
        "SystemConfiguration",
        "UIKit",
    ],
    visibility = ["//visibility:public"],
)

alias(
    name = "android_aar",
    actual = "//library/kotlin/io/envoyproxy/envoymobile:envoy_aar",
    visibility = ["//visibility:public"],
)

aar_import(
    name = "envoy_mobile_android",
    aar = "//library/kotlin/io/envoyproxy/envoymobile:envoy_aar",
    visibility = ["//visibility:public"],
)

alias(
    name = "android_dist_ci",
    actual = "//library/kotlin/io/envoyproxy/envoymobile:envoy_aar_with_artifacts",
)

filegroup(
    name = "android_dist",
    srcs = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_aar",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_aar_objdump_collector",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_aar_pom_xml",
    ],
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

xcodeproj(
    name = "xcodeproj",
    archived_bundles_allowed = True,
    build_mode = "bazel",
    project_name = "Envoy",
    tags = ["manual"],
    targets = [
        # TODO(jpsim): Fix Objective-C app support
        # "//examples/objective-c/hello_world:app",
        "//examples/swift/async_await:app",
        "//examples/swift/hello_world:app",
        "//test/swift/apps/baseline:app",
        "//test/swift/apps/experimental:app",
    ],
)
