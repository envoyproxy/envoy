.. _config_thrift_filters_payload_to_metadata:

Envoy Payload-To-Metadata Filter
================================
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata>`

A typical use case for this filter is to dynamically match a specified payload field of requests
with load balancer subsets. For this, a given payload field's value would be extracted and attached
to the request as dynamic metadata which would then be used to match a subset of endpoints.

We already have :ref:`header-to-metadata filter <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.filters.header_to_metadata.v3.HeaderToMetadata>`
to achieve the similar goal. However, we have two reasons for introducing new :ref:`payload-to-metadata filter
<envoy_v3_api_msg_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata>`:

1. Transports like framed transport don't support THeaders, which is unable to use :ref:`header-to-metadata filter
<envoy_v3_api_msg_extensions.filters.network.thrift_proxy.filters.header_to_metadata.v3.HeaderToMetadata>`.

2. Directly referring to payload field stops envoy relying on that the downstream service always copies the field
to the THeader correctly and guarantees single truth of source.

This filter is configured with :ref:`request_rules
<envoy_v3_api_field_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata.request_rules>`
that will be matched against requests. A
:ref:`field_selector
<envoy_v3_api_field_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata.Rule.field_selector>`
of a :ref:`rule
<envoy_v3_api_msg_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata.Rule>`
represents the head of a linked list, each node of the linked list has a :ref:`name
<envoy_v3_api_field_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata.FieldSelector.name>`
for logging and an :ref:`id
<envoy_v3_api_field_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata.FieldSelector.id>`
for matching. The :ref:`field_selector
<envoy_v3_api_field_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata.Rule.field_selector>`
is tied to a payload field when the linked list corresponds to a downward path which rooted in the top-level of the
request message structure. :ref:`on_present
<envoy_v3_api_field_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata.Rule.on_present>`
is triggered when corresponding the payload is present. Otherwise, :ref:`on_missing
<envoy_v3_api_field_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata.Rule.on_missing>`
is triggered.

Note that if the corresponding payload for a :ref:`rule
<envoy_v3_api_msg_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata.Rule>`
is present but :ref:`on_present
<envoy_v3_api_field_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata.Rule.on_present>`
is missing, no metadata is added for this :ref:`rule
<envoy_v3_api_msg_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata.Rule>`.
. If the corresponding payload for a :ref:`rule
<envoy_v3_api_msg_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata.Rule>`
is an empty string, neither :ref:`on_present
<envoy_v3_api_field_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata.Rule.on_present>`
nor :ref:`on_missing
<envoy_v3_api_field_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata.Rule.on_missing>`
is triggered. i.e., no metadata is added for this :ref:`rule
<envoy_v3_api_msg_extensions.filters.network.thrift_proxy.filters.payload_to_metadata.v3.PayloadToMetadata.Rule>`.

Currently payload to metadata filter doesn't support container type payload, i.e., list, set and map.

We limit the size of a single metadata value which is added by this filter to 1024 bytes.

This filter is designed to support payload passthrough. Performing payload to metadata filter
can do deserialization once, and pass the metadata to other filters. This means that load balancing
decisions, consumed from log and routing could all use payload information with a single parse.
Also notably performing the parsing in payload passthrough buffer will mean deserialization once
and not re-serializing, which is the most performant outcome. Currently there is a redundant buffer
copy until we have `BufferView <https://github.com/envoyproxy/envoy/issues/23901>`_.

If any of the filter chain doesn't support payload passthrough, a customized non-passthrough
filter to setup metadata is encouraged from point of performance view.

Example
-------

A sample filter configuration to route traffic to endpoints based on the presence or
absence of a version payload could be:

.. literalinclude:: _include/payload-to-metadata-filter.yaml
    :language: yaml
    :lines: 20-38
    :lineno-start: 20
    :linenos:
    :caption: :download:`payload-to-metadata-filter.yaml <_include/payload-to-metadata-filter.yaml>`

A corresponding upstream cluster configuration could be:

.. literalinclude:: _include/header-to-metadata-filter.yaml
    :language: yaml
    :lines: 39-49
    :lineno-start: 37
    :linenos:
    :caption: :download:`header-to-metadata-filter.yaml <_include/header-to-metadata-filter.yaml>`

The request thrift structure could be:

.. literalinclude:: _include/request.proto
    :language: proto

This would then allow requests of method name ``foo`` with the ``version`` payload field which is
under ``info`` field set to be matched against endpoints with the corresponding version. Whereas
requests with that payload missing would be matched with the default endpoints.

The regex matching and substitution is similar with :ref:`header to metadata filter <config_thrift_filters_header_to_metadata>`.


Statistics
----------

Currently, this filter generates no statistics.
