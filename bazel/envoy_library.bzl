# DO NOT LOAD THIS FILE. Load envoy_build_system.bzl instead.
# Envoy library targets
load(
    ":envoy_internal.bzl",
    "envoy_copts",
    "envoy_external_dep_path",
    "envoy_linkstatic",
)
load("@com_google_protobuf//:protobuf.bzl", "cc_proto_library", "py_proto_library")
load("@envoy_api//bazel:api_build_system.bzl", "api_cc_py_proto_library")

# As above, but wrapped in list form for adding to dep lists. This smell seems needed as
# SelectorValue values have to match the attribute type. See
# https://github.com/bazelbuild/bazel/issues/2273.
def tcmalloc_external_deps(repository):
    return select({
        repository + "//bazel:disable_tcmalloc": [],
        "//conditions:default": [envoy_external_dep_path("gperftools")],
    })

# Envoy C++ library targets that need no transformations or additional dependencies before being
# passed to cc_library should be specified with this function. Note: this exists to ensure that
# all envoy targets pass through an envoy-declared skylark function where they can be modified
# before being passed to a native bazel function.
def envoy_basic_cc_library(name, deps = [], external_deps = [], **kargs):
    native.cc_library(
        name = name,
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps],
        **kargs
    )

# All Envoy extensions must be tagged with their security hardening stance with
# respect to downstream and upstream data plane threats. These are verbose
# labels intended to make clear the trust that operators may place in
# extensions.
EXTENSION_SECURITY_POSTURES = [
    # This extension is hardened against untrusted downstream traffic. It
    # assumes that the upstream is trusted.
    "robust_to_untrusted_downstream",
    # This extension is hardened against both untrusted downstream and upstream
    # traffic.
    "robust_to_untrusted_downstream_and_upstream",
    # This extension is not hardened and should only be used in deployments
    # where both the downstream and upstream are trusted.
    "requires_trusted_downstream_and_upstream",
    # This is functionally equivalent to
    # requires_trusted_downstream_and_upstream, but acts as a placeholder to
    # allow us to identify extensions that need classifying.
    "unknown",
    # Not relevant to data plane threats, e.g. stats sinks.
    "data_plane_agnostic",
]

EXTENSION_STATUS_VALUES = [
    # This extension is stable and is expected to be production usable.
    "stable",
    # This extension is functional but has not had substantial production burn
    # time, use only with this caveat.
    "alpha",
    # This extension is work-in-progress. Functionality is incomplete and it is
    # not intended for production use.
    "wip",
]

def envoy_cc_extension(
        name,
        security_posture,
        # Only set this for internal, undocumented extensions.
        undocumented = False,
        status = "stable",
        tags = [],
        **kwargs):
    if security_posture not in EXTENSION_SECURITY_POSTURES:
        fail("Unknown extension security posture: " + security_posture)
    if status not in EXTENSION_STATUS_VALUES:
        fail("Unknown extension status: " + status)
    envoy_cc_library(name, tags = tags, **kwargs)

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
        linkstamp = None,
        tags = [],
        deps = [],
        strip_include_prefix = None,
        textual_hdrs = None):
    if tcmalloc_dep:
        deps += tcmalloc_external_deps(repository)

    native.cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        copts = envoy_copts(repository) + copts,
        visibility = visibility,
        tags = tags,
        textual_hdrs = textual_hdrs,
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps] + [
            repository + "//include/envoy/common:base_includes",
            repository + "//source/common/common:fmt_lib",
            envoy_external_dep_path("abseil_flat_hash_map"),
            envoy_external_dep_path("abseil_flat_hash_set"),
            envoy_external_dep_path("abseil_strings"),
            envoy_external_dep_path("spdlog"),
            envoy_external_dep_path("fmtlib"),
        ],
        include_prefix = envoy_include_prefix(native.package_name()),
        alwayslink = 1,
        linkstatic = envoy_linkstatic(),
        linkstamp = select({
            repository + "//bazel:windows_x86_64": None,
            "//conditions:default": linkstamp,
        }),
        strip_include_prefix = strip_include_prefix,
    )

    # Intended for usage by external consumers. This allows them to disambiguate
    # include paths via `external/envoy...`
    native.cc_library(
        name = name + "_with_external_headers",
        hdrs = hdrs,
        copts = envoy_copts(repository) + copts,
        visibility = visibility,
        deps = [":" + name],
        strip_include_prefix = strip_include_prefix,
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

# Transform the package path (e.g. include/envoy/common) into a path for
# exporting the package headers at (e.g. envoy/common). Source files can then
# include using this path scheme (e.g. #include "envoy/common/time.h").
def envoy_include_prefix(path):
    if path.startswith("source/") or path.startswith("include/"):
        return "/".join(path.split("/")[1:])
    return None

# Envoy proto targets should be specified with this function.
def envoy_proto_library(name, external_deps = [], **kwargs):
    api_cc_py_proto_library(
        name,
        # Avoid generating .so, we don't need it, can interfere with builds
        # such as OSS-Fuzz.
        linkstatic = 1,
        visibility = ["//visibility:public"],
        **kwargs
    )
