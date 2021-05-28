#!/usr/bin/env python3

# Validate extension metadata

# This script expects a copy of the envoy source to be located at /source
# Alternatively, you can specify a path to the source dir with `ENVOY_SRCDIR`

import pathlib
import re
import sys
from importlib.util import spec_from_loader, module_from_spec
from importlib.machinery import SourceFileLoader

import yaml

BUILTIN_EXTENSIONS = (
    "envoy.request_id.uuid", "envoy.upstreams.tcp.generic", "envoy.transport_sockets.tls",
    "envoy.upstreams.http.http_protocol_options", "envoy.upstreams.http.generic")

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
    "envoy.access_loggers",
    "envoy.bootstrap",
    "envoy.clusters",
    "envoy.compression.compressor",
    "envoy.compression.decompressor",
    "envoy.filters.http",
    "envoy.filters.http.cache",
    "envoy.filters.listener",
    "envoy.filters.network",
    "envoy.filters.udp_listener",
    "envoy.grpc_credentials",
    "envoy.guarddog_actions",
    "envoy.health_checkers",
    "envoy.http.stateful_header_formatters",
    "envoy.internal_redirect_predicates",
    "envoy.io_socket",
    "envoy.http.original_ip_detection",
    "envoy.matching.common_inputs",
    "envoy.matching.input_matchers",
    "envoy.rate_limit_descriptors",
    "envoy.request_id",
    "envoy.resource_monitors",
    "envoy.retry_host_predicates",
    "envoy.retry_priorities",
    "envoy.stats_sinks",
    "envoy.thrift_proxy.filters",
    "envoy.tracers",
    "envoy.transport_sockets.downstream",
    "envoy.transport_sockets.upstream",
    "envoy.tls.cert_validator",
    "envoy.upstreams",
    "envoy.wasm.runtime",
    "DELIBERATELY_OMITTED",
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

# source/extensions/extensions_build_config.bzl must have a .bzl suffix for Starlark
# import, so we are forced to do this workaround.
_extensions_build_config_spec = spec_from_loader(
    'extensions_build_config',
    SourceFileLoader('extensions_build_config', 'source/extensions/extensions_build_config.bzl'))
extensions_build_config = module_from_spec(_extensions_build_config_spec)
_extensions_build_config_spec.loader.exec_module(extensions_build_config)


def num_read_filters_fuzzed():
    data = pathlib.Path(
        'test/extensions/filters/network/common/fuzz/uber_per_readfilter.cc').read_text()
    # Hack-ish! We only search the first 50 lines to capture the filters in filterNames().
    return len(re.findall('NetworkFilterNames::get()', ''.join(data.splitlines()[:50])))


def num_robust_to_downstream_network_filters(db):
    # Count number of network filters robust to untrusted downstreams.
    return len([
        ext for ext, data in db.items()
        if 'network' in ext and data['security_posture'] == 'robust_to_untrusted_downstream'
    ])


# TODO(phlax): move this to a checker class, and add pytests
def validate_extensions():
    returns = 0

    with open("source/extensions/extensions_metadata.yaml") as f:
        metadata = yaml.safe_load(f.read())

    all_extensions = set(extensions_build_config.EXTENSIONS.keys()) | set(BUILTIN_EXTENSIONS)
    only_metadata = set(metadata.keys()) - all_extensions
    missing_metadata = all_extensions - set(metadata.keys())

    if only_metadata:
        returns = 1
        print(f"Metadata for unused extensions found: {only_metadata}")

    if missing_metadata:
        returns = 1
        print(f"Metadata missing for extensions: {missing_metadata}")

    if num_robust_to_downstream_network_filters(metadata) != num_read_filters_fuzzed():
        returns = 1
        print(
            'Check that all network filters robust against untrusted'
            'downstreams are fuzzed by adding them to filterNames() in'
            'test/extensions/filters/network/common/uber_per_readfilter.cc')

    for k, v in metadata.items():
        if not v["security_posture"]:
            returns = 1
            print(
                f"Missing security posture for {k}. "
                "Please make sure the target is an envoy_cc_extension and security_posture is set")
        elif v["security_posture"] not in EXTENSION_SECURITY_POSTURES:
            print("Unknown extension security posture: {v['security_posture']}")
            returns = 1
        if not v["categories"]:
            returns = 1
            print(
                f"Missing extension category for {k}. "
                "Please make sure the target is an envoy_cc_extension and category is set")
        else:
            for cat in v["categories"]:
                if cat not in EXTENSION_CATEGORIES:
                    returns = 1
                    print(f"Unknown extension category for {k}: {cat}")
        if v["status"] not in EXTENSION_STATUS_VALUES:
            returns = 1
            print(f"Unknown extension status: {v['status']}")

    return returns


if __name__ == '__main__':
    sys.exit(validate_extensions())
