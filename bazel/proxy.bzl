# Bazel 8 Java rules compatibility proxy
# This file provides compatibility for externalized Java rules in Bazel 8.

# Load and re-export all symbols from the main compatibility module
load(
    ":java_compatibility_proxy.bzl",
    _BootClassPathInfo = "BootClassPathInfo",
    _JavaInfo = "JavaInfo",
    _JavaPluginInfo = "JavaPluginInfo",
    _JavaRuntimeInfo = "JavaRuntimeInfo",
    _JavaToolchainInfo = "JavaToolchainInfo",
    _android_ndk_repository = "android_ndk_repository",
    _android_sdk_repository = "android_sdk_repository",
    _android_binary = "android_binary",
    _android_local_test = "android_local_test",
    _android_library = "android_library",
    _aar_import = "aar_import",
    _android_common_function = "android_common_function",
    _android_common_internal_compile = "android_common_internal_compile",
    _AndroidSdkInfo = "AndroidSdkInfo",
    _AndroidNativeLibsInfo = "AndroidNativeLibsInfo",
    _AndroidResourcesInfo = "AndroidResourcesInfo",
    _AndroidAssetsInfo = "AndroidAssetsInfo",
    _AndroidManifestInfo = "AndroidManifestInfo",
    _AndroidLibraryAarInfo = "AndroidLibraryAarInfo",
    _AndroidLibraryResourceClassJarProvider = "AndroidLibraryResourceClassJarProvider",
    _AndroidFeatureFlagSetProvider = "AndroidFeatureFlagSetProvider",
    _AndroidProguardInfo = "AndroidProguardInfo",
    _AndroidIdlProvider = "AndroidIdlProvider",
    _AndroidPreprocessedJarInfo = "AndroidPreprocessedJarInfo",
    _java_binary = "java_binary",
    _java_common = "java_common",
    _java_common_internal_compile = "java_common_internal_compile",
    _java_import = "java_import",
    _java_info_internal_merge = "java_info_internal_merge",
    _java_library = "java_library",
    _java_lite_proto_library = "java_lite_proto_library",
    _java_package_configuration = "java_package_configuration",
    _java_plugin = "java_plugin",
    _java_proto_library = "java_proto_library",
    _java_runtime = "java_runtime",
    _java_test = "java_test",
    _java_toolchain = "java_toolchain",
)

# Re-export all symbols
BootClassPathInfo = _BootClassPathInfo
JavaInfo = _JavaInfo
JavaPluginInfo = _JavaPluginInfo
JavaRuntimeInfo = _JavaRuntimeInfo
JavaToolchainInfo = _JavaToolchainInfo
android_ndk_repository = _android_ndk_repository
android_sdk_repository = _android_sdk_repository
android_binary = _android_binary
android_local_test = _android_local_test
android_library = _android_library
aar_import = _aar_import
android_common_function = _android_common_function
android_common_internal_compile = _android_common_internal_compile
AndroidSdkInfo = _AndroidSdkInfo
AndroidNativeLibsInfo = _AndroidNativeLibsInfo
AndroidResourcesInfo = _AndroidResourcesInfo
AndroidAssetsInfo = _AndroidAssetsInfo
AndroidManifestInfo = _AndroidManifestInfo
AndroidLibraryAarInfo = _AndroidLibraryAarInfo
AndroidLibraryResourceClassJarProvider = _AndroidLibraryResourceClassJarProvider
AndroidFeatureFlagSetProvider = _AndroidFeatureFlagSetProvider
AndroidProguardInfo = _AndroidProguardInfo
AndroidIdlProvider = _AndroidIdlProvider
AndroidPreprocessedJarInfo = _AndroidPreprocessedJarInfo
java_binary = _java_binary
java_common = _java_common
java_common_internal_compile = _java_common_internal_compile
java_import = _java_import
java_info_internal_merge = _java_info_internal_merge
java_library = _java_library
java_lite_proto_library = _java_lite_proto_library
java_package_configuration = _java_package_configuration
java_plugin = _java_plugin
java_proto_library = _java_proto_library
java_runtime = _java_runtime
java_test = _java_test
java_toolchain = _java_toolchain
