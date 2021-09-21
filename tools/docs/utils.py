import abstracts
from functools import cached_property

from google.protobuf import json_format

from udpa.annotations import security_pb2
from udpa.annotations import status_pb2
from validate import validate_pb2

from envoy.docs import abstract

from tools.config_validation import validate_fragment

# data imports
from envoy_repo.contrib import (
    extensions_categories as contrib_extensions_categories)
from envoy_repo.source.extensions import (
    all_extensions_metadata, extension_status_categories,
    extensions_categories)
from envoy_repo.docs import protodoc_manifest, v2_mapping

from tools.api_proto_plugin import annotations

# Last documented v2 api version
ENVOY_LAST_V2_VERSION = "1.17.2"


@abstracts.implementer(abstract.AProtobufRSTFormatter)
class EnvoyProtobufRSTFormatter:

    @property
    def annotations(self):
        return annotations

    @property
    def api_namespace(self):
        return super().api_namespace

    @property
    def json_format(self):
        return json_format

    @property
    def namespace(self):
        return super().namespace

    @property
    def protodoc_manifest(self):
        return protodoc_manifest.data

    @property
    def security_pb2(self):
        return security_pb2

    @property
    def status_pb2(self):
        return status_pb2

    @property
    def validate_pb2(self):
        return validate_pb2


@abstracts.implementer(abstract.ARSTFormatter)
class EnvoyRSTFormatter:

    @property
    def contrib_extensions_categories(self):
        return contrib_extensions_categories.data

    @property
    def contrib_note(self):
        return super().contrib_note

    @property
    def envoy_last_v2_version(self):
        return ENVOY_LAST_V2_VERSION

    @property
    def extension_category_template(self):
        return super().extension_category_template

    @property
    def extension_security_postures(self):
        return extension_status_categories.data["security_postures"]

    @property
    def extension_status_types(self):
        return extension_status_categories.data["status_types"]

    @property
    def extension_template(self):
        return super().extension_template

    @property
    def extensions_categories(self):
        return extensions_categories.data

    @property
    def extensions_metadata(self):
        return all_extensions_metadata.data

    @property
    def validate_fragment(self):
        return validate_fragment.validate_fragment

    @cached_property
    def pb(self):
        return EnvoyProtobufRSTFormatter(self)

    @property
    def v2_link_template(self):
        return super().v2_link_template

    @property
    def v2_mapping(self):
        return v2_mapping.data


rst_formatter = EnvoyRSTFormatter()

__all__ = ("EnvoyRSTFormatter", "rst_formatter")
