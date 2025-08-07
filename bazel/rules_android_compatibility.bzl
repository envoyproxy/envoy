# Comprehensive rules_android compatibility layer for Bazel 8
# This file provides all necessary stubs to replace rules_android functionality

# Android rule stubs
def android_binary(**kwargs):
    """Stub android_binary rule for Bazel 8 compatibility."""
    pass

def android_library(**kwargs):
    """Stub android_library rule for Bazel 8 compatibility."""
    pass

def android_local_test(**kwargs):
    """Stub android_local_test rule for Bazel 8 compatibility."""
    pass

def aar_import(**kwargs):
    """Stub aar_import rule for Bazel 8 compatibility."""
    pass

def android_sdk_repository(**kwargs):
    """Stub android_sdk_repository rule for Bazel 8 compatibility."""
    pass

def android_ndk_repository(**kwargs):
    """Stub android_ndk_repository rule for Bazel 8 compatibility."""
    pass

# Android providers
AndroidSdkInfo = provider(
    doc = "Stub AndroidSdkInfo provider for Bazel 8 compatibility.",
    fields = {},
)

AndroidNativeLibsInfo = provider(
    doc = "Stub AndroidNativeLibsInfo provider for Bazel 8 compatibility.",
    fields = {},
)

AndroidResourcesInfo = provider(
    doc = "Stub AndroidResourcesInfo provider for Bazel 8 compatibility.",
    fields = {},
)

AndroidAssetsInfo = provider(
    doc = "Stub AndroidAssetsInfo provider for Bazel 8 compatibility.",
    fields = {},
)

AndroidManifestInfo = provider(
    doc = "Stub AndroidManifestInfo provider for Bazel 8 compatibility.",
    fields = {},
)

AndroidLibraryAarInfo = provider(
    doc = "Stub AndroidLibraryAarInfo provider for Bazel 8 compatibility.",
    fields = {},
)

AndroidLibraryResourceClassJarProvider = provider(
    doc = "Stub AndroidLibraryResourceClassJarProvider for Bazel 8 compatibility.",
    fields = {},
)

AndroidFeatureFlagSetProvider = provider(
    doc = "Stub AndroidFeatureFlagSetProvider for Bazel 8 compatibility.",
    fields = {},
)

AndroidProguardInfo = provider(
    doc = "Stub AndroidProguardInfo provider for Bazel 8 compatibility.",
    fields = {},
)

AndroidIdlProvider = provider(
    doc = "Stub AndroidIdlProvider for Bazel 8 compatibility.",
    fields = {},
)

AndroidPreprocessedJarInfo = provider(
    doc = "Stub AndroidPreprocessedJarInfo for Bazel 8 compatibility.",
    fields = {},
)

# Android common functions
def android_common(**kwargs):
    """Stub android_common function for Bazel 8 compatibility."""
    return None

def android_common_internal_compile(**kwargs):
    """Stub internal Android compile function for Bazel 8 compatibility."""
    return None

# Android common struct
android_common = struct(
    AndroidSdkInfo = AndroidSdkInfo,
    AndroidNativeLibsInfo = AndroidNativeLibsInfo,
    AndroidResourcesInfo = AndroidResourcesInfo,
    AndroidAssetsInfo = AndroidAssetsInfo,
    AndroidManifestInfo = AndroidManifestInfo,
    AndroidLibraryAarInfo = AndroidLibraryAarInfo,
    AndroidLibraryResourceClassJarProvider = AndroidLibraryResourceClassJarProvider,
    AndroidFeatureFlagSetProvider = AndroidFeatureFlagSetProvider,
    AndroidProguardInfo = AndroidProguardInfo,
    AndroidIdlProvider = AndroidIdlProvider,
    AndroidPreprocessedJarInfo = AndroidPreprocessedJarInfo,
)

# Export all symbols
__all__ = [
    "android_binary", "android_library", "android_local_test", "aar_import",
    "android_sdk_repository", "android_ndk_repository", "android_common",
    "android_common_internal_compile", "AndroidSdkInfo", "AndroidNativeLibsInfo",
    "AndroidResourcesInfo", "AndroidAssetsInfo", "AndroidManifestInfo",
    "AndroidLibraryAarInfo", "AndroidLibraryResourceClassJarProvider",
    "AndroidFeatureFlagSetProvider", "AndroidProguardInfo", "AndroidIdlProvider",
    "AndroidPreprocessedJarInfo",
]
