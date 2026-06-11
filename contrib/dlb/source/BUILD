load(
    "//bazel:envoy_build_system.bzl",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# DLB source targets disabled along with the envoy.network.connection_balance.dlb
# extension. The upstream Intel mirror serving @dlb returns HTTP 202 instead of
# 200 OK, which aborts Bazel's external_http_archive fetch and breaks any build
# of //contrib/dlb/... See https://github.com/envoyproxy/envoy/issues/45491 and
# the matching patch in //contrib:contrib_build_config.bzl.
#
# load("@rules_foreign_cc//foreign_cc:defs.bzl", "make")
# load("//bazel:envoy_build_system.bzl", "envoy_cc_contrib_extension")
# load("//contrib:all_contrib_extensions.bzl", "envoy_contrib_linux_x86_64_constraints")
#
# make(
#     name = "dlb",
#     env = {"DLB_DISABLE_DOMAIN_SERVER": "TRUE"},
#     lib_source = "@dlb//:libdlb",
#     out_static_libs = ["libdlb.a"],
#     postfix_script = "mv libdlb.a $$INSTALLDIR/lib && rm -rf $$INSTALLDIR/include && mkdir -p $$INSTALLDIR/include && cp -L *.h $$INSTALLDIR/include",
#     tags = ["skip_on_windows"],
#     target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
#     targets = ["libdlb.a"],
# )
#
# envoy_cc_contrib_extension(
#     name = "connection_balancer",
#     srcs = select({
#         "//bazel:linux_x86_64": ["connection_balancer_impl.cc"],
#         "//conditions:default": [],
#     }),
#     hdrs = ["connection_balancer_impl.h"],
#     defines = select({
#         "//bazel:linux_x86_64": [],
#         "//conditions:default": ["DLB_DISABLED=1"],
#     }),
#     deps = [
#         "//envoy/api:api_interface",
#         "//envoy/registry",
#         "//envoy/server:factory_context_interface",
#         "//envoy/server:filter_config_interface",
#         "//source/common/common:logger_lib",
#         "//source/common/listener_manager:active_tcp_listener",
#         "//source/common/network:connection_balancer_lib",
#         "//source/common/protobuf:utility_lib",
#         "@envoy_api//contrib/envoy/extensions/network/connection_balance/dlb/v3alpha:pkg_cc_proto",
#     ] + select({
#         "//bazel:linux_x86_64": [":dlb"],
#         "//conditions:default": [],
#     }),
# )
