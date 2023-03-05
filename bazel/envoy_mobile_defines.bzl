# DO NOT LOAD THIS FILE. Load envoy_build_system.bzl instead.
load(":envoy_select.bzl", "envoy_select_admin_functionality", "envoy_select_enable_http3", "envoy_select_envoy_mobile_listener", "envoy_select_envoy_mobile_request_compression")

# Compute the defines needed for Envoy Mobile libraries that don't use Envoy's main library wrappers.
def envoy_mobile_defines(repository):
    return envoy_select_admin_functionality(["ENVOY_ADMIN_FUNCTIONALITY"], repository) + \
           envoy_select_enable_http3(["ENVOY_ENABLE_QUIC"], repository) + \
           envoy_select_envoy_mobile_listener(["ENVOY_MOBILE_ENABLE_LISTENER"], repository) + \
           envoy_select_envoy_mobile_request_compression(["ENVOY_MOBILE_REQUEST_COMPRESSION"], repository)
