.. _config_thrift_filters_header_to_metadata:

Envoy Header-To-Metadata Filter
===============================
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.filters.header_to_metadata.v3.HeaderToMetadata``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.filters.header_to_metadata.v3.HeaderToMetadata>`

This filter is configured with rules that will be matched against requests.
Each rule has either a header and can be triggered either when the header is present or missing.

When a rule is triggered, dynamic metadata will be added based on the configuration of the rule.
If the header is present, it's value is extracted and used along with the specified
key as metadata. If the header is missing, on missing case is triggered and the value
specified is used for adding metadata.

The metadata can then be used for load balancing decisions, consumed from logs, etc.

A typical use case for this filter is to dynamically match requests with load balancer
subsets. For this, a given header's value would be extracted and attached to the request
as dynamic metadata which would then be used to match a subset of endpoints.

Example
-------

A sample filter configuration to route traffic to endpoints based on the presence or
absence of a version header could be:

.. literalinclude:: _include/header-to-metadata-filter.yaml
    :language: yaml
    :lines: 20-35
    :lineno-start: 20
    :linenos:
    :caption: :download:`header-to-metadata-filter.yaml <_include/header-to-metadata-filter.yaml>`

A corresponding upstream cluster configuration could be:

.. literalinclude:: _include/header-to-metadata-filter.yaml
    :language: yaml
    :lines: 37-47
    :lineno-start: 37
    :linenos:
    :caption: :download:`header-to-metadata-filter.yaml <_include/header-to-metadata-filter.yaml>`

This would then allow requests with the ``x-version`` header set to be matched against
endpoints with the corresponding version. Whereas requests with that header missing
would be matched with the default endpoints.

If the header's value needs to be transformed before it's added to the request as
dynamic metadata, this filter supports regex matching and substitution:

.. literalinclude:: _include/header-to-metadata-filter-regex-substitution.yaml
    :language: yaml
    :lines: 20-32
    :lineno-start: 20
    :linenos:
    :caption: :download:`header-to-metadata-filter-regex-substitution.yaml <_include/header-to-metadata-filter-regex-substitution.yaml>`

Statistics
----------

Currently, this filter generates no statistics.
