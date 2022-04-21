#!/usr/bin/env python3

# Validate extension metadata

import json
import pathlib
import re
import sys
from functools import cached_property
from typing import Iterator

from aio.run import checker

from envoy.base import utils

BUILTIN_EXTENSIONS = (
    "envoy.request_id.uuid", "envoy.upstreams.tcp.generic", "envoy.transport_sockets.tls",
    "envoy.upstreams.http.http_protocol_options", "envoy.upstreams.http.generic",
    "envoy.matching.inputs.request_headers", "envoy.matching.inputs.request_trailers",
    "envoy.matching.inputs.response_headers", "envoy.matching.inputs.response_trailers",
    "envoy.matching.inputs.destination_ip", "envoy.matching.inputs.destination_port",
    "envoy.matching.inputs.source_ip", "envoy.matching.inputs.source_port",
    "envoy.matching.inputs.direct_source_ip", "envoy.matching.inputs.source_type",
    "envoy.matching.inputs.server_name", "envoy.matching.inputs.transport_protocol",
    "envoy.matching.inputs.application_protocol")

# All Envoy extensions must be tagged with their security hardening stance with
# respect to downstream and upstream data plane threats. These are verbose
# labels intended to make clear the trust that operators may place in
# extensions.
EXTENSION_SECURITY_POSTURES = (
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
    "data_plane_agnostic")

# Extension categories as defined by factories
EXTENSION_CATEGORIES = (
    "envoy.access_loggers", "envoy.bootstrap", "envoy.clusters", "envoy.compression.compressor",
    "envoy.compression.decompressor", "envoy.config.validators", "envoy.filters.http",
    "envoy.filters.http.cache", "envoy.filters.listener", "envoy.filters.network",
    "envoy.filters.udp_listener", "envoy.formatter", "envoy.grpc_credentials",
    "envoy.guarddog_actions", "envoy.health_checkers", "envoy.http.stateful_header_formatters",
    "envoy.internal_redirect_predicates", "envoy.io_socket", "envoy.http.original_ip_detection",
    "envoy.matching.common_inputs", "envoy.matching.input_matchers", "envoy.tls.key_providers",
    "envoy.quic.proof_source", "envoy.quic.server.crypto_stream", "envoy.rate_limit_descriptors",
    "envoy.request_id", "envoy.resource_monitors", "envoy.retry_host_predicates",
    "envoy.retry_priorities", "envoy.stats_sinks", "envoy.thrift_proxy.filters", "envoy.tracers",
    "envoy.sip_proxy.filters", "envoy.transport_sockets.downstream",
    "envoy.transport_sockets.upstream", "envoy.tls.cert_validator", "envoy.upstreams",
    "envoy.wasm.runtime", "envoy.common.key_value", "envoy.network.dns_resolver",
    "envoy.rbac.matchers", "envoy.access_loggers.extension_filters", "envoy.http.stateful_session",
    "envoy.matching.http.input", "envoy.matching.network.input",
    "envoy.matching.network.custom_matchers")

EXTENSION_STATUS_VALUES = (
    # This extension is stable and is expected to be production usable.
    "stable",
    # This extension is functional but has not had substantial production burn
    # time, use only with this caveat.
    "alpha",
    # This extension is work-in-progress. Functionality is incomplete and it is
    # not intended for production use.
    "wip")

FILTER_NAMES_PATTERN = "NetworkFilterNames::get()"

FUZZ_TEST_PATH = "test/extensions/filters/network/common/fuzz/uber_per_readfilter.cc"

METADATA_PATH = "source/extensions/extensions_metadata.yaml"
CONTRIB_METADATA_PATH = "contrib/extensions_metadata.yaml"


class ExtensionsConfigurationError(Exception):
    pass


class ExtensionsChecker(checker.Checker):
    checks = ("registered", "fuzzed", "metadata")
    extension_categories = EXTENSION_CATEGORIES
    extension_security_postures = EXTENSION_SECURITY_POSTURES
    extension_status_values = EXTENSION_STATUS_VALUES

    @cached_property
    def all_extensions(self) -> set:
        return set(self.configured_extensions.keys()) | set(
            self.configured_contrib_extensions.keys()) | set(BUILTIN_EXTENSIONS)

    @cached_property
    def configured_extensions(self) -> dict:
        return json.loads(pathlib.Path(self.args.build_config).read_text())

    @cached_property
    def configured_contrib_extensions(self) -> dict:
        return json.loads(pathlib.Path(self.args.contrib_build_config).read_text())

    @property
    def fuzzed_count(self) -> int:
        data = pathlib.Path(FUZZ_TEST_PATH).read_text()
        # Hack-ish! We only search the first 50 lines to capture the filters
        # in `filterNames()`.
        return len(re.findall(FILTER_NAMES_PATTERN, "".join(data.splitlines()[:50])))

    @cached_property
    def metadata(self) -> dict:
        result = utils.from_yaml(METADATA_PATH)
        result.update(utils.from_yaml(CONTRIB_METADATA_PATH))
        if not isinstance(result, dict):
            raise ExtensionsConfigurationError(
                f"Unable to parse metadata: {METADATA_PATH} {CONTRIB_METADATA_PATH}")
        return result

    @property
    def robust_to_downstream_count(self) -> int:
        # Count number of network filters robust to untrusted downstreams.
        return len([
            ext for ext, data in self.metadata.items()
            if "network" in ext and data["security_posture"] == "robust_to_untrusted_downstream"
        ])

    def add_arguments(self, parser):
        super().add_arguments(parser)
        parser.add_argument("--build_config")
        parser.add_argument("--contrib_build_config")
        parser.add_argument("--core_extensions")

    async def check_fuzzed(self) -> None:
        if self.robust_to_downstream_count == self.fuzzed_count:
            return
        self.error(
            "fuzzed", [
                "Check that all network filters robust against untrusted "
                f"downstreams are fuzzed by adding them to filterNames() in {FUZZ_TEST_PATH}"
            ])

    async def check_metadata(self) -> None:
        for extension in self.metadata:
            errors = self._check_metadata(extension)
            if errors:
                self.error("metadata", errors)

    async def check_registered(self) -> None:
        only_metadata = set(self.metadata.keys()) - self.all_extensions
        missing_metadata = self.all_extensions - set(self.metadata.keys())

        for extension in only_metadata:
            # Skip envoy_mobile_http_connection_manager as it is built with
            # http_connection_manager
            if extension != "envoy.filters.network.envoy_mobile_http_connection_manager":
                self.error("registered", [f"Metadata for unused extension found: {extension}"])

        for extension in missing_metadata:
            self.error("registered", [f"Metadata missing for extension: {extension}"])

    def _check_metadata(self, extension: str) -> list:
        return (
            list(self._check_metadata_categories(extension))
            + list(self._check_metadata_security_posture(extension))
            + list(self._check_metadata_status(extension)))

    def _check_metadata_categories(self, extension: str) -> Iterator[str]:
        categories = self.metadata[extension].get("categories", ())
        for cat in categories:
            if cat not in self.extension_categories:
                yield (
                    f"Unknown extension category for {extension}: {cat}. "
                    "Please add it to tools/extensions/extensions_check.py")
        if not categories:
            yield (
                f"Missing extension category for {extension}. "
                "Please make sure the target is an envoy_cc_extension and category is set")

    def _check_metadata_security_posture(self, extension: str) -> Iterator[str]:
        security_posture = self.metadata[extension]["security_posture"]
        if not security_posture:
            yield (
                f"Missing security posture for {extension}. "
                "Please make sure the target is an envoy_cc_extension and security_posture is set")
        elif security_posture not in self.extension_security_postures:
            yield f"Unknown security posture for {extension}: {security_posture}"

    def _check_metadata_status(self, extension: str) -> Iterator[str]:
        status = self.metadata[extension]["status"]
        if status not in self.extension_status_values:
            yield f"Unknown status for {extension}: {status}"


def main(*args) -> int:
    return ExtensionsChecker(*args)()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
