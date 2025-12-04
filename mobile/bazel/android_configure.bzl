"""Repository rule for Android SDK and NDK autoconfiguration.

This rule is a no-op unless the required android environment variables are set.
"""

# Based on https://github.com/tensorflow/tensorflow/tree/34c03ed67692eb76cb3399cebca50ea8bcde064c/third_party/android
# Workaround for https://github.com/bazelbuild/bazel/issues/14260

_ANDROID_NDK_HOME = "ANDROID_NDK_HOME"
_ANDROID_SDK_HOME = "ANDROID_HOME"

def _android_autoconf_impl(repository_ctx):
    sdk_home = repository_ctx.os.environ.get(_ANDROID_SDK_HOME)
    ndk_home = repository_ctx.os.environ.get(_ANDROID_NDK_HOME)

    sdk_api_level = repository_ctx.attr.sdk_api_level
    ndk_api_level = repository_ctx.attr.ndk_api_level
    build_tools_version = repository_ctx.attr.build_tools_version

    sdk_rule = ""
    if sdk_home:
        sdk_rule = """
    android_sdk_repository(
        name="androidsdk",
        path="{}",
        api_level={},
        build_tools_version="{}",
    )
""".format(sdk_home, sdk_api_level, build_tools_version)

    ndk_rule = ""
    if ndk_home:
        ndk_rule = """
    android_ndk_repository(
        name="androidndk",
        path="{}",
        api_level={},
    )
    native.register_toolchains("@androidndk//:all")

""".format(ndk_home, ndk_api_level)

    if ndk_rule == "" and sdk_rule == "":
        sdk_rule = "pass"

    loads = ""
    if sdk_rule != "" and sdk_rule != "pass":
        loads += 'load("@rules_android//android:rules.bzl", "android_sdk_repository")\n'
    if ndk_rule != "":
        loads += 'load("@rules_android_ndk//:rules.bzl", "android_ndk_repository")'

    repository_ctx.file("BUILD.bazel", "")
    repository_ctx.file("android_configure.bzl", """
{}

def android_workspace():
    {}
    {}
    """.format(loads, sdk_rule, ndk_rule))

android_configure = repository_rule(
    implementation = _android_autoconf_impl,
    environ = [
        _ANDROID_NDK_HOME,
        _ANDROID_SDK_HOME,
    ],
    attrs = {
        "sdk_api_level": attr.int(mandatory = True),
        "ndk_api_level": attr.int(mandatory = True),
        "build_tools_version": attr.string(mandatory = True),
    },
)
