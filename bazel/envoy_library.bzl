load("@rules_cc//cc:defs.bzl", "cc_library")

# DO NOT LOAD THIS FILE. Load envoy_build_system.bzl instead.
# Envoy library targets
load(
    ":envoy_internal.bzl",
    "envoy_copts",
    "envoy_external_dep_path",
    "envoy_linkstatic",
)
load("@envoy_api//bazel:api_build_system.bzl", "api_cc_py_proto_library")
load(
    "@envoy_build_config//:extensions_build_config.bzl",
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
        repository + "//bazel:linux_x86_64": [envoy_external_dep_path("tcmalloc")],
        repository + "//bazel:linux_aarch64": [envoy_external_dep_path("tcmalloc")],
        "//conditions:default": [envoy_external_dep_path("gperftools")],
    })

# Envoy C++ library targets that need no transformations or additional dependencies before being
# passed to cc_library should be specified with this function. Note: this exists to ensure that
# all envoy targets pass through an envoy-declared Starlark function where they can be modified
# before being passed to a native bazel function.
def envoy_basic_cc_library(name, deps = [], external_deps = [], **kargs):
    cc_library(
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

# Extension categories as defined by factories
EXTENSION_CATEGORIES = [
    "envoy.access_logger.extension_filters",
    "envoy.access_loggers",
    "envoy.bootstrap",
    "envoy.clusters",
    "envoy.compression.compressor",
    "envoy.compression.decompressor",
    "envoy.dubbo_proxy.filters",
    "envoy.dubbo_proxy.protocols",
    "envoy.dubbo_proxy.route_matchers",
    "envoy.dubbo_proxy.serializers",
    "envoy.fatal_action",
    "envoy.filters.http",
    "envoy.filters.listener",
    "envoy.filters.network",
    "envoy.filters.udp_listener",
    "envoy.filters.upstream_network",
    "envoy.formatter",
    "envoy.grpc_credentials",
    "envoy.guarddog_actions",
    "envoy.health_checkers",
    "envoy.http.cache",
    "envoy.internal_redirect_predicates",
    "envoy.matching.action",
    "envoy.matching.input_matcher",
    "envoy.quic_client_codec",
    "envoy.quic_server_codec",
    "envoy.rate_limit_descriptors",
    "envoy.request_id_extension",
    "envoy.resolvers",
    "envoy.resource_monitors",
    "envoy.retry_host_predicates",
    "envoy.retry_priorities",
    "envoy.singleton",
    "envoy.ssl_context_manager",
    "envoy.stats_sinks",
    "envoy.thrift_proxy.filters",
    "envoy.thrift_proxy.protocols",
    "envoy.thrift_proxy.transports",
    "envoy.tls_handshakers",
    "envoy.tls.key_providers",
    "envoy.tracers",
    "envoy.transport_sockets.downstream",
    "envoy.transport_sockets.upstream",
    "envoy.typed_metadata",
    "envoy.udp_listeners",
    "envoy.udp_packet_writers",
    "envoy.upstream_options",
    "envoy.upstreams",
    "test",
    "testing",
    "testing.published",
    "testing.published.additional.category",
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
        category = None,
        # Only set this for internal, undocumented extensions.
        undocumented = False,
        status = "stable",
        tags = [],
        extra_visibility = [],
        visibility = EXTENSION_CONFIG_VISIBILITY,
        **kwargs):
    if not category:
        fail("Category not set for %s" % name)
    if type(category) == "string":
        category = (category, )
    for cat in category or []:
        if cat not in EXTENSION_CATEGORIES:
            fail("Unknown extension category for %s: %s"
                 % (name, cat))
    if security_posture not in EXTENSION_SECURITY_POSTURES:
        fail("Unknown extension security posture: " + security_posture)
    if status not in EXTENSION_STATUS_VALUES:
        fail("Unknown extension status: " + status)
    if "//visibility:public" not in visibility:
        visibility = visibility + extra_visibility

    ext_name = name + "_envoy_extension"
    envoy_cc_library(
        name = name,
        tags = tags,
        visibility = visibility,
        **kwargs
    )
    cc_library(
        name = ext_name,
        deps = select({
            ":is_enabled": [":" + name],
            "//conditions:default": [],
        }),
        visibility = visibility,
    )

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
        textual_hdrs = None,
        defines = []):
    if tcmalloc_dep:
        deps += tcmalloc_external_deps(repository)

    cc_library(
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
        strip_include_prefix = strip_include_prefix,
        defines = defines,
    )

    # Intended for usage by external consumers. This allows them to disambiguate
    # include paths via `external/envoy...`
    cc_library(
        name = name + "_with_external_headers",
        hdrs = hdrs,
        copts = envoy_copts(repository) + copts,
        visibility = visibility,
        tags = ["nocompdb"] + tags,
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
