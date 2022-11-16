# DO NOT LOAD THIS FILE. Load envoy_build_system.bzl instead.
# Envoy binary targets
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

# Envoy C++ binary targets should be specified with this function.
def envoy_cc_binary(
        name,
        srcs = [],
        data = [],
        testonly = 0,
        visibility = None,
        external_deps = [],
        repository = "",
        stamped = False,
        deps = [],
        linkopts = [],
        tags = [],
        features = []):
    linker_inputs = envoy_exported_symbols_input()

    if not linkopts:
        linkopts = _envoy_linkopts()
    if stamped:
        linkopts = linkopts + _envoy_stamped_linkopts()
        deps = deps + _envoy_stamped_deps()
    linkopts += envoy_dbg_linkopts()
    deps = deps + [envoy_external_dep_path(dep) for dep in external_deps] + envoy_stdlib_deps()
    native.cc_binary(
        name = name,
        srcs = srcs,
        data = data,
        additional_linker_inputs = linker_inputs,
        copts = envoy_copts(repository),
        linkopts = linkopts,
        testonly = testonly,
        linkstatic = 1,
        visibility = visibility,
        malloc = tcmalloc_external_dep(repository),
        stamp = 1,
        deps = deps,
        tags = tags,
        features = features,
    )

# Compute the final linkopts based on various options.
def _envoy_linkopts():
    return select({
        "@envoy//bazel:apple": [],
        "@envoy//bazel:windows_opt_build": [
            "-DEFAULTLIB:ws2_32.lib",
            "-DEFAULTLIB:iphlpapi.lib",
            "-DEFAULTLIB:shell32.lib",
            "-DEBUG:FULL",
            "-WX",
        ],
        "@envoy//bazel:windows_x86_64": [
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
        "@envoy//bazel:apple": [],
        "@envoy//bazel:boringssl_fips": [],
        "@envoy//bazel:windows_x86_64": [],
        "//conditions:default": ["-pie"],
    }) + envoy_select_exported_symbols(["-Wl,-E"])

def _envoy_stamped_deps():
    return select({
        "@envoy//bazel:windows_x86_64": [],
        "@envoy//bazel:apple": [
            "@envoy//bazel:raw_build_id.ldscript",
        ],
        "//conditions:default": [
            "@envoy//bazel:gnu_build_id.ldscript",
        ],
    })

def _envoy_stamped_linkopts():
    return select({
        # Coverage builds in CI are failing to link when setting a build ID.
        #
        # /usr/bin/ld.gold: internal error in write_build_id, at ../../gold/layout.cc:5419
        "@envoy//bazel:coverage_build": [],
        "@envoy//bazel:windows_x86_64": [],

        # macOS doesn't have an official equivalent to the `.note.gnu.build-id`
        # ELF section, so just stuff the raw ID into a new text section.
        "@envoy//bazel:apple": [
            "-sectcreate __TEXT __build_id",
            "$(location @envoy//bazel:raw_build_id.ldscript)",
        ],

        # Note: assumes GNU GCC (or compatible) handling of `--build-id` flag.
        "//conditions:default": [
            "-Wl,@$(location @envoy//bazel:gnu_build_id.ldscript)",
        ],
    })
