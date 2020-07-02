workspace(name = "envoy_mobile")

load("@envoy_mobile//bazel:envoy_mobile_repositories.bzl", "envoy_mobile_repositories")
envoy_mobile_repositories()

local_repository(
    name = "envoy",
    path = "envoy",
)

local_repository(
    name = "envoy_build_config",
    path = "envoy_build_config",
)

load("@envoy//bazel:api_binding.bzl", "envoy_api_binding")
envoy_api_binding()

load("@envoy//bazel:api_repositories.bzl", "envoy_api_dependencies")
envoy_api_dependencies()

load("@envoy//bazel:repositories.bzl", "envoy_dependencies")
envoy_dependencies()

load("@envoy//bazel:repositories_extra.bzl", "envoy_dependencies_extra")
envoy_dependencies_extra()

load("@envoy//bazel:dependency_imports.bzl", "envoy_dependency_imports")
envoy_dependency_imports()

load("@envoy_mobile//bazel:envoy_mobile_swift_bazel_support.bzl", "swift_support")
swift_support()

load("@envoy_mobile//bazel:envoy_mobile_dependencies.bzl", "envoy_mobile_dependencies")
envoy_mobile_dependencies()

load("@envoy_mobile//bazel:envoy_mobile_toolchains.bzl", "envoy_mobile_toolchains")
envoy_mobile_toolchains()

# Fixing to API 24. Need to lower to API 21: https://github.com/lyft/envoy-mobile/issues/936
# Note: proguard is failing for API 30+
android_sdk_repository(name = "androidsdk", api_level = 24)

android_ndk_repository(name = "androidndk", api_level = 24)
