# DO NOT LOAD THIS FILE. Targets from this file should be considered private
# and not used outside of the @envoy//bazel package.
load("@bazel_skylib//lib:selects.bzl", "selects")
load(":envoy_select.bzl", "envoy_select_admin_html", "envoy_select_disable_exceptions", "envoy_select_disable_logging", "envoy_select_google_grpc", "envoy_select_hot_restart", "envoy_select_nghttp2", "envoy_select_signal_trace", "envoy_select_static_extension_registration")

_APPLE = Label("//bazel:apple")
_APPLE_NON_OPT = Label("//bazel:apple_non_opt")
_CLANG_BUILD = Label("//bazel:clang_build")
_CLANG_CL_BUILD = Label("//bazel:clang_cl_build")
_CLANG_CL_DBG_BUILD = Label("//bazel:clang_cl_dbg_build")
_CLANG_CL_FASTBUILD_BUILD = Label("//bazel:clang_cl_fastbuild_build")
_CLANG_CL_OPT_BUILD = Label("//bazel:clang_cl_opt_build")
_DBG_BUILD = Label("//bazel:dbg_build")
_DEBUG_TCMALLOC = Label("//bazel:debug_tcmalloc")
_DISABLE_DEPRECATED_FEATURES = Label("//bazel:disable_deprecated_features")
_DISABLE_KNOWN_ISSUE_ASSERTS = Label("//bazel:disable_known_issue_asserts")
_DISABLE_OBJECT_DUMP_ON_SIGNAL_TRACE = Label("//bazel:disable_object_dump_on_signal_trace")
_DISABLE_TCMALLOC = Label("//bazel:disable_tcmalloc")
_DYNAMIC_LINK_TESTS = Label("//bazel:dynamic_link_tests")
_ENABLE_EXECUTION_CONTEXT = Label("//bazel:enable_execution_context")
_ENABLE_EXPORTED_SYMBOLS = Label("//bazel:enable_exported_symbols")
_ENABLE_LOG_DEBUG_ASSERT_IN_RELEASE = Label("//bazel:enable_log_debug_assert_in_release")
_ENABLE_LOG_FAST_DEBUG_ASSERT_IN_RELEASE = Label("//bazel:enable_log_fast_debug_assert_in_release")
_ENABLE_PATH_NORMALIZATION_BY_DEFAULT = Label("//bazel:enable_path_normalization_by_default")
_ENABLE_PERF_ANNOTATION = Label("//bazel:enable_perf_annotation")
_ENABLE_PERF_TRACING = Label("//bazel:enable_perf_tracing")
_EXPORTED_SYMBOLS = Label("//bazel:exported_symbols.txt")
_EXPORTED_SYMBOLS_APPLE = Label("//bazel:exported_symbols_apple.txt")
_FASTBUILD_BUILD = Label("//bazel:fastbuild_build")
_FORCE_LIBCPP = Label("//bazel:force_libcpp")
_GCC_BUILD = Label("//bazel:gcc_build")
_GPERFTOOLS = Label("//bazel/external:gperftools")
_GPERFTOOLS_TCMALLOC = Label("//bazel:gperftools_tcmalloc")
_JEMALLOC = Label("//bazel/deps:jemalloc")
_JEMALLOC_ENABLED = Label("//bazel:jemalloc_enabled")
_LINUX = Label("//bazel:linux")
_NO_DEBUG_INFO = Label("//bazel:no_debug_info")
_OPT_BUILD = Label("//bazel:opt_build")
_STATIC_STDLIB = Label("//bazel:static_stdlib")
_TCMALLOC_LIB = Label("//bazel:tcmalloc_lib")
_UHV_ENABLED = Label("//bazel:uhv_enabled")
_WINDOWS_DBG_BUILD = Label("//bazel:windows_dbg_build")
_WINDOWS_FASTBUILD_BUILD = Label("//bazel:windows_fastbuild_build")
_WINDOWS_OPT_BUILD = Label("//bazel:windows_opt_build")
_WINDOWS_X86_64 = Label("//bazel:windows_x86_64")

_DEPRECATED_REPOSITORY_MESSAGE = """\
The `repository` argument is deprecated and only accepts \"\" or \"@envoy\".
Use the `@envoy//bazel` label_flag overrides instead, for example \
`--@envoy//bazel:test_main=@your_repo//:custom_test_main`.
"""

def validate_repository(caller, repository):
    if repository not in ("", "@envoy"):
        fail("%s: %s Got %r." % (caller, _DEPRECATED_REPOSITORY_MESSAGE, repository))

# Compute the final copts based on various options.
def envoy_copts(test = False):
    posix_options = [
        "-Wall",
        "-Wextra",
        "-Werror",
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
              _WINDOWS_X86_64: msvc_options,
               "//conditions:default": posix_options,
           }) + select({
               # Simplify the amount of symbolic debug info for test binaries, since
               # debugging info detailing some 1600 test binaries would be wasteful.
               # targets listed in order from generic to increasing specificity.
               # Bazel adds an implicit -DNDEBUG for opt targets.
               _OPT_BUILD: [] if test else ["-ggdb3"],
               _FASTBUILD_BUILD: [],
               _DBG_BUILD: ["-ggdb3"],
               _WINDOWS_OPT_BUILD: [] if test else ["-Z7"],
               _WINDOWS_FASTBUILD_BUILD: [],
               _WINDOWS_DBG_BUILD: [],
               _CLANG_CL_OPT_BUILD: [] if test else ["-Z7", "-fstandalone-debug"],
               _CLANG_CL_FASTBUILD_BUILD: ["-fno-standalone-debug"],
               _CLANG_CL_DBG_BUILD: ["-fstandalone-debug"],
           }) + select({
               # Toggle expected features and warnings by compiler
               _CLANG_BUILD: [
                   "-fno-limit-debug-info",
                   "-Wgnu-conditional-omitted-operand",
                   "-Wc++2a-extensions",
                   "-Wrange-loop-analysis",
               ],
               _GCC_BUILD: [
                   "-Wno-maybe-uninitialized",
                   # Don't disable overloaded-virtual here; just fix it with `using` if it comes up,
                   # see https://github.com/envoyproxy/envoy/pull/41887 for an example.
               ],
               # Allow 'nodiscard' function results values to be discarded for test code only
               # TODO(envoyproxy/windows-dev): Replace /Zc:preprocessor with /experimental:preprocessor
               # for msvc versions between 15.8 through 16.4.x. see
               # https://docs.microsoft.com/en-us/cpp/build/reference/zc-preprocessor
               _WINDOWS_X86_64: ["-wd4834", "-Zc:preprocessor", "-Wv:19.4"] if test else ["-Zc:preprocessor", "-Wv:19.4"],
               _CLANG_CL_BUILD: ["-Wno-unused-result"] if test else [],
               "//conditions:default": [],
           }) + select({
               # TODO: Remove once https://reviews.llvm.org/D73007 is in the lowest supported Xcode version
               _APPLE: ["-Wno-range-loop-analysis"],
               "//conditions:default": [],
           }) + select({
               _NO_DEBUG_INFO: ["-g0"],
               "//conditions:default": [],
           }) + selects.with_or({
               _DISABLE_TCMALLOC: ["-DABSL_MALLOC_HOOK_MMAP_DISABLE"],
               _DEBUG_TCMALLOC: ["-DENVOY_MEMORY_DEBUG_ENABLED=1", "-DGPERFTOOLS_TCMALLOC"],
               _GPERFTOOLS_TCMALLOC: ["-DGPERFTOOLS_TCMALLOC"],
               _JEMALLOC_ENABLED: ["-DJEMALLOC"],
               (
                   "@platforms//cpu:x86_64",
                   "@platforms//cpu:aarch64",
               ): ["-DTCMALLOC"],
               "//conditions:default": ["-DGPERFTOOLS_TCMALLOC"],
           }) + select({
               _DISABLE_OBJECT_DUMP_ON_SIGNAL_TRACE: [],
               "//conditions:default": ["-DENVOY_OBJECT_TRACE_ON_DUMP"],
           }) + select({
               _DISABLE_DEPRECATED_FEATURES: ["-DENVOY_DISABLE_DEPRECATED_FEATURES"],
               "//conditions:default": [],
           }) + select({
               _ENABLE_LOG_DEBUG_ASSERT_IN_RELEASE: ["-DENVOY_LOG_DEBUG_ASSERT_IN_RELEASE"],
               "//conditions:default": [],
           }) + select({
               _ENABLE_LOG_FAST_DEBUG_ASSERT_IN_RELEASE: ["-DENVOY_LOG_FAST_DEBUG_ASSERT_IN_RELEASE"],
               "//conditions:default": [],
           }) + select({
               _DISABLE_KNOWN_ISSUE_ASSERTS: ["-DENVOY_DISABLE_KNOWN_ISSUE_ASSERTS"],
               "//conditions:default": [],
           }) + select({
               # APPLE_USE_RFC_3542 is needed to support IPV6_PKTINFO in MAC OS.
               _APPLE: ["-D__APPLE_USE_RFC_3542"],
               "//conditions:default": [],
           }) + select({
               _UHV_ENABLED: ["-DENVOY_ENABLE_UHV"],
               "//conditions:default": [],
           }) + envoy_select_hot_restart(["-DENVOY_HOT_RESTART"]) + \
           envoy_select_nghttp2(["-DENVOY_NGHTTP2"]) + \
           envoy_select_disable_exceptions(["-fno-exceptions"]) + \
           envoy_select_admin_html(["-DENVOY_ADMIN_HTML"]) + \
           envoy_select_static_extension_registration(["-DENVOY_STATIC_EXTENSION_REGISTRATION"]) + \
           envoy_select_disable_logging(["-DENVOY_DISABLE_LOGGING"]) + \
           _envoy_select_perf_annotation(["-DENVOY_PERF_ANNOTATION"]) + \
           _envoy_select_execution_context() + \
           _envoy_select_perfetto(["-DENVOY_PERFETTO"]) + \
           envoy_select_google_grpc(["-DENVOY_GOOGLE_GRPC"]) + \
           envoy_select_signal_trace(["-DENVOY_HANDLE_SIGNALS"]) + \
           _envoy_select_path_normalization_by_default(["-DENVOY_NORMALIZE_PATH_BY_DEFAULT"])

# Mapping of external dependency short names to their actual Bazel targets.
# This replaces the need for native.bind() calls and //external: references.
EXTERNAL_DEPS_MAP = {
    # Abseil
    "abseil_strings": "@abseil-cpp//absl/strings",
    # gRPC transcoding
    "grpc_transcoding": "@grpc-httpjson-transcoding//src:transcoding",
    "path_matcher": "@grpc-httpjson-transcoding//src:path_matcher",
    # Google APIs
    "api_httpbody_protos": "@googleapis//google/api:httpbody_cc_proto",
    "http_api_protos": "@googleapis//google/api:annotations_cc_proto",
    # nghttp2
    "nghttp2": Label("//bazel/deps:nghttp2"),
    # gRPC
    "grpc": "@grpc//:grpc++",
    "grpc_health_proto": "@grpc//src/proto/grpc/health/v1:health_cc_proto",
    # SSL/Crypto (aliases defined in @envoy//bazel)
    "ssl": Label("//bazel:ssl"),
    "crypto": Label("//bazel:crypto"),
    # Bazel tools
    "bazel_runfiles": "@bazel_tools//tools/cpp/runfiles",
}

# References to Envoy external dependencies should be wrapped with this function.
def envoy_external_dep_path(dep):
    if dep in EXTERNAL_DEPS_MAP:
        return EXTERNAL_DEPS_MAP[dep]

    fail("Unknown external dependency '%s'. Add it to EXTERNAL_DEPS_MAP in bazel/envoy_internal.bzl" % dep)

def envoy_linkstatic():
    return select({
        _DYNAMIC_LINK_TESTS: 0,
        "//conditions:default": 1,
    })

def envoy_select_force_libcpp(if_libcpp, default = None):
    return select({
        _FORCE_LIBCPP: if_libcpp,
        _APPLE: [],
        _WINDOWS_X86_64: [],
        "//conditions:default": default or [],
    })

def envoy_stdlib_deps():
    return select({
        "//conditions:default": [_STATIC_STDLIB],
    })

def envoy_dbg_linkopts():
    return select({
        # TODO: Remove once we have https://github.com/bazelbuild/bazel/pull/15635
        _APPLE_NON_OPT: ["-Wl,-no_deduplicate"],
        "//conditions:default": [],
    })

# Dependencies on tcmalloc_and_profiler should be wrapped with this function.
def tcmalloc_external_dep():
    return selects.with_or({
        _DISABLE_TCMALLOC: None,
        (
            _DEBUG_TCMALLOC,
            _GPERFTOOLS_TCMALLOC,
        ): _GPERFTOOLS,
        _JEMALLOC_ENABLED: _JEMALLOC,
        "//conditions:default": _TCMALLOC_LIB,
    })

# Select the given values if default path normalization is on in the current build.
def _envoy_select_path_normalization_by_default(xs):
    return select({
        _ENABLE_PATH_NORMALIZATION_BY_DEFAULT: xs,
        "//conditions:default": [],
    })

def _envoy_select_perf_annotation(xs):
    return select({
        _ENABLE_PERF_ANNOTATION: xs,
        "//conditions:default": [],
    })

def _envoy_select_execution_context():
    return select({
        _ENABLE_EXECUTION_CONTEXT: ["-DENVOY_ENABLE_EXECUTION_CONTEXT"],
        "//conditions:default": [],
    })

def _envoy_select_perfetto(xs):
    return select({
        _ENABLE_PERF_TRACING: xs,
        "//conditions:default": [],
    })

def envoy_exported_symbols_input():
    return [
        _EXPORTED_SYMBOLS,
        _EXPORTED_SYMBOLS_APPLE,
    ]

# Default symbols to be exported.
def _envoy_default_exported_symbols():
    return select({
        _LINUX: [
            "-Wl,--dynamic-list=$(location %s)" % str(_EXPORTED_SYMBOLS),
        ],
        _APPLE: [
            "-Wl,-exported_symbols_list,$(location %s)" % str(_EXPORTED_SYMBOLS_APPLE),
        ],
        "//conditions:default": [],
    })

# Select the given values if exporting is enabled in the current build.
def envoy_select_exported_symbols(xs):
    return select({
        _ENABLE_EXPORTED_SYMBOLS: xs,
        "//conditions:default": [],
    }) + _envoy_default_exported_symbols()
