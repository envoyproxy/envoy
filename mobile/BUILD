load("@com_github_buildbuddy_io_rules_xcodeproj//xcodeproj:project_options.bzl", "project_options")
load("@com_github_buildbuddy_io_rules_xcodeproj//xcodeproj:top_level_target.bzl", "top_level_targets")
load("@com_github_buildbuddy_io_rules_xcodeproj//xcodeproj:xcodeproj.bzl", "xcodeproj")
load("@com_github_buildbuddy_io_rules_xcodeproj//xcodeproj:xcschemes.bzl", "xcschemes")
load("@rules_android//android:rules.bzl", "aar_import")
load("@rules_apple//apple:apple.bzl", "apple_static_framework_import")
load("@rules_kotlin//kotlin/internal:toolchains.bzl", "define_kt_toolchain")
load("//bazel:framework_imports_extractor.bzl", "framework_imports_extractor")

licenses(["notice"])  # Apache 2

exports_files([
    ".bazelrc",
    "MODULE.bazel",
    "MODULE.bazel.lock",
    "VERSION",
    "pyproject.toml",
])

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
    deps = [
        "@maven//:androidx_annotation_annotation",
        "@maven//:androidx_core_core",
        "@maven//:org_jetbrains_annotations",
        "@maven//:org_jetbrains_kotlin_kotlin_stdlib",
    ],
)

alias(
    name = "android_dist",
    actual = "//library/kotlin/io/envoyproxy/envoymobile:envoy_aar_with_artifacts",
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
    bazel_path = "bazel",
    project_name = "Envoy",
    project_options = project_options(
        indent_width = 2,
        tab_width = 2,
    ),
    scheme_autogeneration_mode = "auto",  # Switch to "all" to generate schemes for all deps
    tags = ["manual"],
    top_level_targets = [
        # Apps
        top_level_targets(
            labels = [
                "//examples/objective-c/hello_world:app",
                "//examples/swift/hello_world:app",
                "//test/swift/apps/experimental:app",
            ],
            target_environments = [
                "device",
                "simulator",
            ],
        ),
        # Tests
        "//experimental/swift:quic_stream_test",
        "//test/objective-c:envoy_bridge_utility_test",
        "//test/swift/cxx:test",
        "//test/swift/integration:test",
        "//test/swift/stats:test",
        "//test/swift:test",
    ],
    xcschemes = [
        xcschemes.scheme(
            name = "Hello World App",
            run = xcschemes.run(launch_target = "//examples/swift/hello_world:app"),
        ),
        xcschemes.scheme(
            name = "Hello World App (ObjC)",
            run = xcschemes.run(launch_target = "//examples/objective-c/hello_world:app"),
        ),
        xcschemes.scheme(
            name = "Experimental App",
            run = xcschemes.run(launch_target = "//test/swift/apps/experimental:app"),
        ),
        xcschemes.scheme(
            name = "Swift Library",
            run = xcschemes.run(build_targets = [
                xcschemes.top_level_anchor_target(
                    "//examples/swift/hello_world:app",
                    library_targets = ["//library/swift:ios_lib"],
                ),
            ]),
        ),
        xcschemes.scheme(
            name = "iOS Tests",
            test = xcschemes.test(test_targets = [
                "//experimental/swift:quic_stream_test",
                "//test/objective-c:envoy_bridge_utility_test",
                "//test/swift/integration:test",
                "//test/swift/stats:test",
                "//test/swift:test",
            ]),
        ),
        xcschemes.scheme(
            name = "Objective-C Library",
            run = xcschemes.run(build_targets = [
                xcschemes.top_level_anchor_target(
                    "//examples/swift/hello_world:app",
                    library_targets = ["//library/objective-c:envoy_engine_objc_lib"],
                ),
            ]),
            test = xcschemes.test(test_targets = ["//test/objective-c:envoy_bridge_utility_test"]),
        ),
    ],
)
