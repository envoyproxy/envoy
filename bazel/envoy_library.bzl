# DO NOT LOAD THIS FILE. Load envoy_build_system.bzl instead.
# Envoy library targets
load(
    ":envoy_internal.bzl",
    "envoy_copts",
    "envoy_external_dep_path",
    "envoy_linkstatic",
)
load(":envoy_pch.bzl", "envoy_pch_copts")
load("@envoy_api//bazel:api_build_system.bzl", "api_cc_py_proto_library")
load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "CONTRIB_EXTENSION_PACKAGE_VISIBILITY",
    "EXTENSION_CONFIG_VISIBILITY",
)

# As above, but wrapped in list form for adding to dep lists. This smell seems needed as
# SelectorValue values have to match the attribute type. See
# https://github.com/bazelbuild/bazel/issues/2273.
def tcmalloc_external_deps(repository):
    return select({
        repository + "//bazel:disable_tcmalloc": [],
        repository + "//bazel:disable_tcmalloc_on_linux_x86_64": [],
        repository + "//bazel:disable_tcmalloc_on_linux_aarch64": [],
        repository + "//bazel:debug_tcmalloc": [envoy_external_dep_path("gperftools")],
        repository + "//bazel:debug_tcmalloc_on_linux_x86_64": [envoy_external_dep_path("gperftools")],
        repository + "//bazel:debug_tcmalloc_on_linux_aarch64": [envoy_external_dep_path("gperftools")],
        repository + "//bazel:gperftools_tcmalloc": [envoy_external_dep_path("gperftools")],
        repository + "//bazel:gperftools_tcmalloc_on_linux_x86_64": [envoy_external_dep_path("gperftools")],
        repository + "//bazel:gperftools_tcmalloc_on_linux_aarch64": [envoy_external_dep_path("gperftools")],
        repository + "//bazel:linux_x86_64": [
            envoy_external_dep_path("tcmalloc"),
            envoy_external_dep_path("tcmalloc_profile_marshaler"),
            envoy_external_dep_path("tcmalloc_malloc_extension"),
        ],
        repository + "//bazel:linux_aarch64": [
            envoy_external_dep_path("tcmalloc"),
            envoy_external_dep_path("tcmalloc_profile_marshaler"),
            envoy_external_dep_path("tcmalloc_malloc_extension"),
        ],
        "//conditions:default": [envoy_external_dep_path("gperftools")],
    })

# Envoy C++ library targets that need no transformations or additional dependencies before being
# passed to cc_library should be specified with this function. Note: this exists to ensure that
# all envoy targets pass through an envoy-declared Starlark function where they can be modified
# before being passed to a native bazel function.
def envoy_basic_cc_library(name, deps = [], external_deps = [], **kargs):
    native.cc_library(
        name = name,
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps],
        **kargs
    )

def envoy_cc_extension(
        name,
        tags = [],
        extra_visibility = [],
        visibility = EXTENSION_CONFIG_VISIBILITY,
        alwayslink = 1,
        **kwargs):
    if "//visibility:public" not in visibility:
        visibility = visibility + extra_visibility

    ext_name = name + "_envoy_extension"
    envoy_cc_library(
        name = name,
        tags = tags,
        visibility = visibility,
        alwayslink = alwayslink,
        **kwargs
    )
    native.cc_library(
        name = ext_name,
        tags = tags,
        deps = select({
            ":is_enabled": [":" + name],
            "//conditions:default": [],
        }),
        visibility = visibility,
    )

def envoy_cc_contrib_extension(
        name,
        tags = [],
        extra_visibility = [],
        visibility = CONTRIB_EXTENSION_PACKAGE_VISIBILITY,
        alwayslink = 1,
        **kwargs):
    envoy_cc_extension(name, tags, extra_visibility, visibility, **kwargs)

# Envoy C++ library targets should be specified with this function.
def envoy_cc_library(
        name,
        srcs = [],
        hdrs = [],
        copts = [],
        visibility = None,
        external_deps = [],
        tcmalloc_dep = None,
        repository = "",
        tags = [],
        deps = [],
        strip_include_prefix = None,
        include_prefix = None,
        textual_hdrs = None,
        alwayslink = None,
        defines = []):
    if tcmalloc_dep:
        deps += tcmalloc_external_deps(repository)

    # If alwayslink is not specified, allow turning it off via --define=library_autolink=disabled
    # alwayslink is defaulted on for envoy_cc_extensions to ensure the REGISTRY macros work.
    if alwayslink == None:
        alwayslink = select({
            repository + "//bazel:disable_library_autolink": 0,
            "//conditions:default": 1,
        })

    native.cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        copts = envoy_copts(repository) + envoy_pch_copts(repository, "//source/common/common:common_pch") + copts,
        visibility = visibility,
        tags = tags,
        textual_hdrs = textual_hdrs,
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps] + [
            repository + "//envoy/common:base_includes",
            repository + "//source/common/common:fmt_lib",
            repository + "//source/common/common:common_pch",
            envoy_external_dep_path("abseil_flat_hash_map"),
            envoy_external_dep_path("abseil_flat_hash_set"),
            envoy_external_dep_path("abseil_strings"),
            envoy_external_dep_path("fmtlib"),
        ],
        alwayslink = alwayslink,
        linkstatic = envoy_linkstatic(),
        strip_include_prefix = strip_include_prefix,
        include_prefix = include_prefix,
        defines = defines,
    )

    # Intended for usage by external consumers. This allows them to disambiguate
    # include paths via `external/envoy...`
    native.cc_library(
        name = name + "_with_external_headers",
        hdrs = hdrs,
        copts = envoy_copts(repository) + copts,
        visibility = visibility,
        tags = ["nocompdb"] + tags,
        deps = [":" + name],
        strip_include_prefix = strip_include_prefix,
        include_prefix = include_prefix,
    )

# Used to specify a library that only builds on POSIX
def envoy_cc_posix_library(name, srcs = [], hdrs = [], **kargs):
    envoy_cc_library(
        name = name + "_posix",
        srcs = select({
            "@envoy//bazel:windows_x86_64": [],
            "//conditions:default": srcs,
        }),
        hdrs = select({
            "@envoy//bazel:windows_x86_64": [],
            "//conditions:default": hdrs,
        }),
        **kargs
    )

# Used to specify a library that only builds on POSIX excluding Linux
def envoy_cc_posix_without_linux_library(name, srcs = [], hdrs = [], **kargs):
    envoy_cc_library(
        name = name + "_posix",
        srcs = select({
            "@envoy//bazel:windows_x86_64": [],
            "@envoy//bazel:linux": [],
            "//conditions:default": srcs,
        }),
        hdrs = select({
            "@envoy//bazel:windows_x86_64": [],
            "@envoy//bazel:linux": [],
            "//conditions:default": hdrs,
        }),
        **kargs
    )

# Used to specify a library that only builds on Linux
def envoy_cc_linux_library(name, srcs = [], hdrs = [], **kargs):
    envoy_cc_library(
        name = name + "_linux",
        srcs = select({
            "@envoy//bazel:linux": srcs,
            "//conditions:default": [],
        }),
        hdrs = select({
            "@envoy//bazel:linux": hdrs,
            "//conditions:default": [],
        }),
        **kargs
    )

# Used to specify a library that only builds on Windows
def envoy_cc_win32_library(name, srcs = [], hdrs = [], **kargs):
    envoy_cc_library(
        name = name + "_win32",
        srcs = select({
            "@envoy//bazel:windows_x86_64": srcs,
            "//conditions:default": [],
        }),
        hdrs = select({
            "@envoy//bazel:windows_x86_64": hdrs,
            "//conditions:default": [],
        }),
        **kargs
    )

# Envoy proto targets should be specified with this function.
def envoy_proto_library(name, **kwargs):
    api_cc_py_proto_library(
        name,
        # Avoid generating .so, we don't need it, can interfere with builds
        # such as OSS-Fuzz.
        linkstatic = 1,
        visibility = ["//visibility:public"],
        **kwargs
    )
