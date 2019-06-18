# DO NOT LOAD THIS FILE. Targets from this file should be considered private
# and not used outside of the @envoy//bazel package.
load(":envoy_select.bzl", "envoy_select_google_grpc", "envoy_select_hot_restart")

# Compute the final copts based on various options.
def envoy_copts(repository, test = False):
    posix_options = [
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wnon-virtual-dtor",
        "-Woverloaded-virtual",
        "-Wold-style-cast",
        "-Wvla",
        "-std=c++14",
    ]

    msvc_options = [
        "-WX",
        "-Zc:__cplusplus",
        "-std:c++14",
        "-DWIN32",
        "-DWIN32_LEAN_AND_MEAN",
        # need win8 for ntohll
        # https://msdn.microsoft.com/en-us/library/windows/desktop/aa383745(v=vs.85).aspx
        "-D_WIN32_WINNT=0x0602",
        "-DNTDDI_VERSION=0x06020000",
        "-DCARES_STATICLIB",
        "-DNGHTTP2_STATICLIB",
    ]

    return select({
               repository + "//bazel:windows_x86_64": msvc_options,
               "//conditions:default": posix_options,
           }) + select({
               # Bazel adds an implicit -DNDEBUG for opt.
               repository + "//bazel:opt_build": [] if test else ["-ggdb3"],
               repository + "//bazel:fastbuild_build": [],
               repository + "//bazel:dbg_build": ["-ggdb3"],
               repository + "//bazel:windows_opt_build": [],
               repository + "//bazel:windows_fastbuild_build": [],
               repository + "//bazel:windows_dbg_build": [],
           }) + select({
               repository + "//bazel:disable_tcmalloc": ["-DABSL_MALLOC_HOOK_MMAP_DISABLE"],
               "//conditions:default": ["-DTCMALLOC"],
           }) + select({
               repository + "//bazel:debug_tcmalloc": ["-DENVOY_MEMORY_DEBUG_ENABLED=1"],
               "//conditions:default": [],
           }) + select({
               repository + "//bazel:disable_signal_trace": [],
               "//conditions:default": ["-DENVOY_HANDLE_SIGNALS"],
           }) + select({
               repository + "//bazel:enable_log_debug_assert_in_release": ["-DENVOY_LOG_DEBUG_ASSERT_IN_RELEASE"],
               "//conditions:default": [],
           }) + select({
               # APPLE_USE_RFC_3542 is needed to support IPV6_PKTINFO in MAC OS.
               repository + "//bazel:apple": ["-D__APPLE_USE_RFC_3542"],
               "//conditions:default": [],
           }) + envoy_select_hot_restart(["-DENVOY_HOT_RESTART"], repository) + \
           _envoy_select_perf_annotation(["-DENVOY_PERF_ANNOTATION"]) + \
           envoy_select_google_grpc(["-DENVOY_GOOGLE_GRPC"], repository) + \
           _envoy_select_path_normalization_by_default(["-DENVOY_NORMALIZE_PATH_BY_DEFAULT"], repository)

# References to Envoy external dependencies should be wrapped with this function.
def envoy_external_dep_path(dep):
    return "//external:%s" % dep

def envoy_linkstatic():
    return select({
        "@envoy//bazel:asan_build": 0,
        "//conditions:default": 1,
    })

def envoy_select_force_libcpp(if_libcpp, default = None):
    return select({
        "@envoy//bazel:force_libcpp": if_libcpp,
        "@envoy//bazel:apple": [],
        "@envoy//bazel:windows_x86_64": [],
        "//conditions:default": default or [],
    })

def envoy_static_link_libstdcpp_linkopts():
    return envoy_select_force_libcpp(
        # TODO(PiotrSikora): statically link libc++ once that's possible.
        # See: https://reviews.llvm.org/D53238
        ["-stdlib=libc++"],
        ["-static-libstdc++", "-static-libgcc"],
    )

# Dependencies on tcmalloc_and_profiler should be wrapped with this function.
def tcmalloc_external_dep(repository):
    return select({
        repository + "//bazel:disable_tcmalloc": None,
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
