# DO NOT LOAD THIS FILE. Load envoy_build_system.bzl instead.
# Envoy binary targets
load("@rules_cc//cc:defs.bzl", "cc_binary")
load(
    ":envoy_internal.bzl",
    "envoy_copts",
    "envoy_dbg_linkopts",
    "envoy_exported_symbols_input",
    "envoy_external_dep_path",
    "envoy_select_exported_symbols",
    "envoy_stdlib_deps",
    "tcmalloc_external_dep",
)
load(":envoy_select.bzl", "deprecate_repository")

_APPLE = Label("//bazel:apple")
_COVERAGE_BUILD = Label("//bazel:coverage_build")
_ENGFLOW_RBE_X86_64 = Label("//bazel:engflow_rbe_x86_64")
_FIPS_BUILD = Label("//bazel:fips_build")
_GNU_BUILD_ID = Label("//bazel:gnu_build_id.ldscript")
_RAW_BUILD_ID = Label("//bazel:raw_build_id.ldscript")
_WINDOWS_OPT_BUILD = Label("//bazel:windows_opt_build")
_WINDOWS_X86_64 = Label("//bazel:windows_x86_64")

# Envoy C++ binary targets should be specified with this function.
def envoy_cc_binary(
        name,
        srcs = [],
        data = [],
        testonly = 0,
        visibility = None,
        rbe_pool = None,
        exec_properties = {},
        external_deps = [],
        repository = "",
        stamp = 1,
        stamped = False,
        deps = [],
        linkopts = [],
        tags = [],
        features = [],
        linkstatic = True):
    exec_properties = exec_properties | select({
        _ENGFLOW_RBE_X86_64: {"Pool": rbe_pool} if rbe_pool else {},
        "//conditions:default": {},
    })
    linker_inputs = envoy_exported_symbols_input()

    if not linkopts:
        linkopts = _envoy_linkopts()
    if stamped:
        linkopts = linkopts + _envoy_stamped_linkopts()
        deps = deps + _envoy_stamped_deps()
    linkopts += envoy_dbg_linkopts()
    deps = deps + [envoy_external_dep_path(dep) for dep in external_deps] + envoy_stdlib_deps() + deprecate_repository("envoy_cc_binary", repository)
    cc_binary(
        name = name,
        srcs = srcs,
        data = data,
        additional_linker_inputs = linker_inputs,
        copts = envoy_copts(),
        exec_properties = exec_properties,
        linkopts = linkopts,
        testonly = testonly,
        linkstatic = linkstatic,
        visibility = visibility,
        malloc = tcmalloc_external_dep(),
        stamp = stamp,
        deps = deps,
        tags = tags,
        features = features,
    )

# Compute the final linkopts based on various options.
def _envoy_linkopts():
    return select({
        _APPLE: [
            # https://github.com/envoyproxy/envoy/issues/24782
            "-Wl,-framework,CoreFoundation",
            # https://github.com/bazelbuild/bazel/pull/16414
            "-Wl,-undefined,error",
        ],
        _WINDOWS_OPT_BUILD: [
            "-DEFAULTLIB:ws2_32.lib",
            "-DEFAULTLIB:iphlpapi.lib",
            "-DEFAULTLIB:shell32.lib",
            "-DEBUG:FULL",
            "-WX",
        ],
        _WINDOWS_X86_64: [
            "-DEFAULTLIB:ws2_32.lib",
            "-DEFAULTLIB:iphlpapi.lib",
            "-DEFAULTLIB:shell32.lib",
            "-WX",
        ],
        "//conditions:default": [
            "-pthread",
            "-lrt",
            "-ldl",
            "-Wl,-z,relro,-z,now",
            "-Wl,--hash-style=gnu",
        ],
    }) + select({
        _APPLE: [],
        _FIPS_BUILD: [],
        _WINDOWS_X86_64: [],
        "//conditions:default": ["-pie"],
    }) + envoy_select_exported_symbols(["-Wl,-E"])

def _envoy_stamped_deps():
    return select({
        _WINDOWS_X86_64: [],
        _APPLE: [
            _RAW_BUILD_ID,
        ],
        "//conditions:default": [
            _GNU_BUILD_ID,
        ],
    })

def _envoy_stamped_linkopts():
    return select({
        # Coverage builds in CI are failing to link when setting a build ID.
        #
        # /usr/bin/ld.gold: internal error in write_build_id, at ../../gold/layout.cc:5419
        _COVERAGE_BUILD: [],
        _WINDOWS_X86_64: [],

        # macOS doesn't have an official equivalent to the `.note.gnu.build-id`
        # ELF section, so just stuff the raw ID into a new text section.
        _APPLE: [
            "-sectcreate __TEXT __build_id",
            "$(location %s)" % str(_RAW_BUILD_ID),
        ],

        # Note: assumes GNU GCC (or compatible) handling of `--build-id` flag.
        "//conditions:default": [
            "-Wl,@$(location %s)" % str(_GNU_BUILD_ID),
        ],
    })
