load("//bazel:envoy_build_system.bzl", "envoy_benchmark_test", "envoy_cc_benchmark_binary", "envoy_cc_mock", "envoy_cc_test", "envoy_cc_test_binary", "envoy_cc_test_library")
load("//source/extensions:all_extensions.bzl", "envoy_all_extensions")
load("//bazel:repositories.bzl", "PPC_SKIP_TARGETS", "WINDOWS_SKIP_TARGETS")

# All extension tests should use this version of envoy_cc_test(). It allows compiling out
# tests for extensions that the user does not wish to include in their build.
# @param extension_name should match an extension listed in EXTENSIONS.
def envoy_extension_cc_test(
        name,
        extension_name,
        **kwargs):
    actual_rule = envoy_cc_test(name, **kwargs)
    windows_rule = actual_rule
    ppc_rule = actual_rule
    print(extension_name)
    print(WINDOWS_SKIP_TARGETS)
    if extension_name in WINDOWS_SKIP_TARGETS:
        print("extension in skip list")
        windows_rule = None
    if extension_name in PPC_SKIP_TARGETS:
        ppc_rule = None

    return select({
        "//bazel:windows_x86_64": windows_rule,
        "//bazel:linux_ppc": ppc_rule,
        "//conditions:default": actual_rule,
    })

    # extensions = envoy_all_extensions(WINDOWS_SKIP_TARGETS)
    # if extension_name in extensions:
    #     return
    #
    # envoy_cc_test(name, **kwargs)

def envoy_extension_cc_test_library(
        name,
        extension_name,
        **kwargs):
    actual_rule = envoy_cc_test_library(name, **kwargs)
    windows_rule = actual_rule
    ppc_rule = actual_rule
    if extension_name in WINDOWS_SKIP_TARGETS:
        windows_rule = None
    if extension_name in PPC_SKIP_TARGETS:
        ppc_rule = None

    return select({
        "//bazel:windows_x86_64": windows_rule,
        "//bazel:linux_ppc": ppc_rule,
        "//conditions:default": actual_rule,
    })

    # extensions = envoy_all_extensions(WINDOWS_SKIP_TARGETS)
    # if extension_name in extensions:
    #     return
    #
    # envoy_cc_test_library(name, **kwargs)

def envoy_extension_cc_mock(
        name,
        extension_name,
        **kwargs):
    actual_rule = envoy_cc_mock(name, **kwargs)
    windows_rule = actual_rule
    ppc_rule = actual_rule
    if extension_name in WINDOWS_SKIP_TARGETS:
        windows_rule = None
    if extension_name in PPC_SKIP_TARGETS:
        ppc_rule = None

    return select({
        "//bazel:windows_x86_64": windows_rule,
        "//bazel:linux_ppc": ppc_rule,
        "//conditions:default": actual_rule,
    })

    # extensions = envoy_all_extensions(WINDOWS_SKIP_TARGETS)
    # if extension_name in extensions:
    #     return
    #
    # envoy_cc_mock(name, **kwargs)

def envoy_extension_cc_test_binary(
        name,
        extension_name,
        **kwargs):
    actual_rule = envoy_cc_test_binary(name, **kwargs)
    windows_rule = actual_rule
    ppc_rule = actual_rule
    if extension_name in WINDOWS_SKIP_TARGETS:
        windows_rule = None
    if extension_name in PPC_SKIP_TARGETS:
        ppc_rule = None

    return select({
        "//bazel:windows_x86_64": windows_rule,
        "//bazel:linux_ppc": ppc_rule,
        "//conditions:default": actual_rule,
    })

    # extensions = envoy_all_extensions(WINDOWS_SKIP_TARGETS)
    # if extension_name in extensions:
    #     return
    #
    # envoy_cc_test_binary(name, **kwargs)

def envoy_extension_cc_benchmark_binary(
        name,
        extension_name,
        **kwargs):
    actual_rule = envoy_cc_benchmark_binary(name, **kwargs)
    windows_rule = actual_rule
    ppc_rule = actual_rule
    if extension_name in WINDOWS_SKIP_TARGETS:
        windows_rule = None
    if extension_name in PPC_SKIP_TARGETS:
        ppc_rule = None

    return select({
        "//bazel:windows_x86_64": windows_rule,
        "//bazel:linux_ppc": ppc_rule,
        "//conditions:default": actual_rule,
    })

    # extensions = envoy_all_extensions(WINDOWS_SKIP_TARGETS)
    # if extension_name in extensions:
    #     return
    #
    # envoy_cc_benchmark_binary(name, **kwargs)

def envoy_extension_benchmark_test(
        name,
        extension_name,
        **kwargs):
    actual_rule = envoy_benchmark_test(name, **kwargs)
    windows_rule = actual_rule
    ppc_rule = actual_rule
    if extension_name in WINDOWS_SKIP_TARGETS:
        windows_rule = None
    if extension_name in PPC_SKIP_TARGETS:
        ppc_rule = None

    return select({
        "//bazel:windows_x86_64": windows_rule,
        "//bazel:linux_ppc": ppc_rule,
        "//conditions:default": actual_rule,
    })

    # extensions = envoy_all_extensions(WINDOWS_SKIP_TARGETS)
    # if extension_name in extensions:
    #     return
    #
    # envoy_benchmark_test(name, **kwargs)
