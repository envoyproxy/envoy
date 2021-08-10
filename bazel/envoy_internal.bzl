# DO NOT LOAD THIS FILE. Targets from this file should be considered private
# and not used outside of the @envoy//bazel package.
load(":envoy_select.bzl", "envoy_select_enable_http3", "envoy_select_google_grpc", "envoy_select_hot_restart")

# Compute the final copts based on various options.
def envoy_copts(repository, test = False):
    posix_options = [
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wnon-virtual-dtor",
        "-Woverloaded-virtual",
        "-Wold-style-cast",
        "-Wformat",
        "-Wformat-security",
        "-Wvla",
        "-Wno-deprecated-declarations",
        "-Wreturn-type",
    ]

    # Windows options for cleanest service compilation;
    #   General MSVC C++ options for Envoy current expectations.
    #   Target windows.h for all Windows 10 (0x0A) API prototypes (ntohll etc)
    #   (See https://msdn.microsoft.com/en-us/library/windows/desktop/aa383745(v=vs.85).aspx )
    #   Optimize Windows headers by dropping GUI-oriented features from compilation
    msvc_options = [
        "-WX",
        "-Zc:__cplusplus",
        "-DWIN32",
        "-D_WIN32_WINNT=0x0A00",  # _WIN32_WINNT_WIN10
        "-DNTDDI_VERSION=0x0A000005",  # NTDDI_WIN10_RS4
        "-DWIN32_LEAN_AND_MEAN",
        "-DNOUSER",
        "-DNOMCX",
        "-DNOIME",
        "-DNOCRYPT",
        # Ignore unguarded gcc pragmas in quiche (unrecognized by MSVC)
        "-wd4068",
        # Silence incorrect MSVC compiler warnings when converting between std::optional
        # data types (while conversions between primitive types are producing no error)
        "-wd4244",
        # Allow inline functions to be undefined
        "-wd4506",
    ]

    return select({
               repository + "//bazel:windows_x86_64": msvc_options,
               "//conditions:default": posix_options,
           }) + select({
               # Simplify the amount of symbolic debug info for test binaries, since
               # debugging info detailing some 1600 test binaries would be wasteful.
               # targets listed in order from generic to increasing specificity.
               # Bazel adds an implicit -DNDEBUG for opt targets.
               repository + "//bazel:opt_build": [] if test else ["-ggdb3", "-gsplit-dwarf"],
               repository + "//bazel:fastbuild_build": [],
               repository + "//bazel:dbg_build": ["-ggdb3", "-gsplit-dwarf"],
               repository + "//bazel:windows_opt_build": [] if test else ["-Z7"],
               repository + "//bazel:windows_fastbuild_build": [],
               repository + "//bazel:windows_dbg_build": [],
               repository + "//bazel:clang_cl_opt_build": [] if test else ["-Z7", "-fstandalone-debug"],
               repository + "//bazel:clang_cl_fastbuild_build": ["-fno-standalone-debug"],
               repository + "//bazel:clang_cl_dbg_build": ["-fstandalone-debug"],
           }) + select({
               # Toggle expected features and warnings by compiler
               repository + "//bazel:clang_build": [
                   "-fno-limit-debug-info",
                   "-Wgnu-conditional-omitted-operand",
                   "-Wc++2a-extensions",
                   "-Wrange-loop-analysis",
               ],
               repository + "//bazel:gcc_build": ["-Wno-maybe-uninitialized"],
               # Allow 'nodiscard' function results values to be discarded for test code only
               # TODO(envoyproxy/windows-dev): Replace /Zc:preprocessor with /experimental:preprocessor
               # for msvc versions between 15.8 through 16.4.x. see
               # https://docs.microsoft.com/en-us/cpp/build/reference/zc-preprocessor
               repository + "//bazel:windows_x86_64": ["-wd4834", "-Zc:preprocessor", "-Wv:19.4"] if test else ["-Zc:preprocessor", "-Wv:19.4"],
               repository + "//bazel:clang_cl_build": ["-Wno-unused-result"] if test else [],
               "//conditions:default": [],
           }) + select({
               # TODO: Remove once https://reviews.llvm.org/D73007 is in the lowest supported Xcode version
               repository + "//bazel:apple": ["-Wno-range-loop-analysis"],
               "//conditions:default": [],
           }) + select({
               repository + "//bazel:no_debug_info": ["-g0"],
               "//conditions:default": [],
           }) + select({
               repository + "//bazel:disable_tcmalloc": ["-DABSL_MALLOC_HOOK_MMAP_DISABLE"],
               repository + "//bazel:disable_tcmalloc_on_linux_x86_64": ["-DABSL_MALLOC_HOOK_MMAP_DISABLE"],
               repository + "//bazel:disable_tcmalloc_on_linux_aarch64": ["-DABSL_MALLOC_HOOK_MMAP_DISABLE"],
               repository + "//bazel:gperftools_tcmalloc": ["-DGPERFTOOLS_TCMALLOC"],
               repository + "//bazel:gperftools_tcmalloc_on_linux_x86_64": ["-DGPERFTOOLS_TCMALLOC"],
               repository + "//bazel:gperftools_tcmalloc_on_linux_aarch64": ["-DGPERFTOOLS_TCMALLOC"],
               repository + "//bazel:debug_tcmalloc": ["-DENVOY_MEMORY_DEBUG_ENABLED=1", "-DGPERFTOOLS_TCMALLOC"],
               repository + "//bazel:debug_tcmalloc_on_linux_x86_64": ["-DENVOY_MEMORY_DEBUG_ENABLED=1", "-DGPERFTOOLS_TCMALLOC"],
               repository + "//bazel:debug_tcmalloc_on_linux_aarch64": ["-DENVOY_MEMORY_DEBUG_ENABLED=1", "-DGPERFTOOLS_TCMALLOC"],
               repository + "//bazel:linux_x86_64": ["-DTCMALLOC"],
               repository + "//bazel:linux_aarch64": ["-DTCMALLOC"],
               "//conditions:default": ["-DGPERFTOOLS_TCMALLOC"],
           }) + select({
               repository + "//bazel:disable_signal_trace": [],
               "//conditions:default": ["-DENVOY_HANDLE_SIGNALS"],
           }) + select({
               repository + "//bazel:disable_object_dump_on_signal_trace": [],
               "//conditions:default": ["-DENVOY_OBJECT_TRACE_ON_DUMP"],
           }) + select({
               repository + "//bazel:disable_deprecated_features": ["-DENVOY_DISABLE_DEPRECATED_FEATURES"],
               "//conditions:default": [],
           }) + select({
               repository + "//bazel:enable_log_debug_assert_in_release": ["-DENVOY_LOG_DEBUG_ASSERT_IN_RELEASE"],
               "//conditions:default": [],
           }) + select({
               repository + "//bazel:enable_log_fast_debug_assert_in_release": ["-DENVOY_LOG_FAST_DEBUG_ASSERT_IN_RELEASE"],
               "//conditions:default": [],
           }) + select({
               repository + "//bazel:disable_known_issue_asserts": ["-DENVOY_DISABLE_KNOWN_ISSUE_ASSERTS"],
               "//conditions:default": [],
           }) + select({
               # APPLE_USE_RFC_3542 is needed to support IPV6_PKTINFO in MAC OS.
               repository + "//bazel:apple": ["-D__APPLE_USE_RFC_3542"],
               "//conditions:default": [],
           }) + envoy_select_hot_restart(["-DENVOY_HOT_RESTART"], repository) + \
           envoy_select_enable_http3(["-DENVOY_ENABLE_QUIC"], repository) + \
           _envoy_select_perf_annotation(["-DENVOY_PERF_ANNOTATION"]) + \
           envoy_select_google_grpc(["-DENVOY_GOOGLE_GRPC"], repository) + \
           _envoy_select_path_normalization_by_default(["-DENVOY_NORMALIZE_PATH_BY_DEFAULT"], repository)

# References to Envoy external dependencies should be wrapped with this function.
def envoy_external_dep_path(dep):
    return "//external:%s" % dep

def envoy_linkstatic():
    return select({
        "@envoy//bazel:dynamic_link_tests": 0,
        "//conditions:default": 1,
    })

def envoy_select_force_libcpp(if_libcpp, default = None):
    return select({
        "@envoy//bazel:force_libcpp": if_libcpp,
        "@envoy//bazel:apple": [],
        "@envoy//bazel:windows_x86_64": [],
        "//conditions:default": default or [],
    })

def envoy_stdlib_deps():
    return select({
        "@envoy//bazel:asan_build": ["@envoy//bazel:dynamic_stdlib"],
        "@envoy//bazel:msan_build": ["@envoy//bazel:dynamic_stdlib"],
        "@envoy//bazel:tsan_build": ["@envoy//bazel:dynamic_stdlib"],
        "//conditions:default": ["@envoy//bazel:static_stdlib"],
    })

# Dependencies on tcmalloc_and_profiler should be wrapped with this function.
def tcmalloc_external_dep(repository):
    return select({
        repository + "//bazel:disable_tcmalloc": None,
        repository + "//bazel:disable_tcmalloc_on_linux_x86_64": None,
        repository + "//bazel:disable_tcmalloc_on_linux_aarch64": None,
        repository + "//bazel:debug_tcmalloc": envoy_external_dep_path("gperftools"),
        repository + "//bazel:debug_tcmalloc_on_linux_x86_64": envoy_external_dep_path("gperftools"),
        repository + "//bazel:debug_tcmalloc_on_linux_aarch64": envoy_external_dep_path("gperftools"),
        repository + "//bazel:gperftools_tcmalloc": envoy_external_dep_path("gperftools"),
        repository + "//bazel:gperftools_tcmalloc_on_linux_x86_64": envoy_external_dep_path("gperftools"),
        repository + "//bazel:gperftools_tcmalloc_on_linux_aarch64": envoy_external_dep_path("gperftools"),
        repository + "//bazel:linux_x86_64": envoy_external_dep_path("tcmalloc"),
        repository + "//bazel:linux_aarch64": envoy_external_dep_path("tcmalloc"),
        "//conditions:default": envoy_external_dep_path("gperftools"),
    })

# Select the given values if default path normalization is on in the current build.
def _envoy_select_path_normalization_by_default(xs, repository = ""):
    return select({
        repository + "//bazel:enable_path_normalization_by_default": xs,
        "//conditions:default": [],
    })

def _envoy_select_perf_annotation(xs):
    return select({
        "@envoy//bazel:enable_perf_annotation": xs,
        "//conditions:default": [],
    })
