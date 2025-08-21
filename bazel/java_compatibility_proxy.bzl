# Bazel 8 Java rules compatibility proxy
# This file provides Java rule symbols that have been externalized in Bazel 8.
# It resolves the compatibility_proxy cycle issue during Bazel 8 migration.

# Define all required provider stubs
JavaRuntimeInfo = provider(
    doc = "Stub JavaRuntimeInfo provider for Bazel 8 compatibility.",
    fields = {},
)

JavaToolchainInfo = provider(
    doc = "Stub JavaToolchainInfo provider for Bazel 8 compatibility.",
    fields = {},
)

BootClassPathInfo = provider(
    doc = "Stub BootClassPathInfo provider for Bazel 8 compatibility.",
    fields = {},
)

JavaPluginInfo = provider(
    doc = "Stub JavaPluginInfo provider for Bazel 8 compatibility.",
    fields = {},
)

JavaInfo = provider(
    doc = "Stub JavaInfo provider for Bazel 8 compatibility.",
    fields = {},
)

# Complete java_common struct with all providers
java_common = struct(
    JavaRuntimeInfo = JavaRuntimeInfo,
    JavaToolchainInfo = JavaToolchainInfo,
    BootClassPathInfo = BootClassPathInfo,
    JavaPluginInfo = JavaPluginInfo,
    JavaInfo = JavaInfo,
)

# All Java rule stubs needed for Bazel 8 compatibility
def java_toolchain(**kwargs):
    """Stub java_toolchain rule for Bazel 8 compatibility."""
    pass

def java_runtime(**kwargs):
    """Stub java_runtime rule for Bazel 8 compatibility."""
    pass

def java_test(**kwargs):
    """Stub java_test rule for Bazel 8 compatibility."""
    pass

def java_binary(**kwargs):
    """Stub java_binary rule for Bazel 8 compatibility."""
    pass

def java_library(**kwargs):
    """Stub java_library rule for Bazel 8 compatibility."""
    pass

def java_import(**kwargs):
    """Stub java_import rule for Bazel 8 compatibility."""
    pass

def java_plugin(**kwargs):
    """Stub java_plugin rule for Bazel 8 compatibility."""
    pass

def java_package_configuration(**kwargs):
    """Stub java_package_configuration rule for Bazel 8 compatibility."""
    pass

def java_proto_library(**kwargs):
    """Stub java_proto_library rule for Bazel 8 compatibility."""
    pass

def java_lite_proto_library(**kwargs):
    """Stub java_lite_proto_library rule for Bazel 8 compatibility."""
    pass

# Internal Java functions for proto support
def java_common_internal_compile(**kwargs):
    """Stub internal Java compile function for Bazel 8 compatibility."""
    return None

def java_info_internal_merge(**kwargs):
    """Stub internal Java info merge function for Bazel 8 compatibility."""
    return None

# Android rule stubs for Bazel 8 compatibility
def android_sdk_repository(**kwargs):
    """Stub android_sdk_repository rule for Bazel 8 compatibility."""
    pass

def android_ndk_repository(**kwargs):
    """Stub android_ndk_repository rule for Bazel 8 compatibility."""
    pass

def android_binary(**kwargs):
    """Stub android_binary rule for Bazel 8 compatibility."""
    pass

def android_local_test(**kwargs):
    """Stub android_local_test rule for Bazel 8 compatibility."""
    pass

def android_library(**kwargs):
    """Stub android_library rule for Bazel 8 compatibility."""
    pass

def aar_import(**kwargs):
    """Stub aar_import rule for Bazel 8 compatibility."""
    pass

# Additional Android providers and functions that might be needed
def android_common_function(**kwargs):
    """Stub android_common function for Bazel 8 compatibility."""
    return None

def android_common_internal_compile(**kwargs):
    """Stub internal Android compile function for Bazel 8 compatibility."""
    return None

# Android provider stubs
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

# Note: AndroidLibraryResourceClassJarProvider is already defined above

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
