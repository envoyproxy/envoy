.. _config_http_filters_header_to_metadata:

Envoy Header-To-Metadata Filter
===============================
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.header_to_metadata.v3.Config``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.header_to_metadata.v3.Config>`

This filter is configured with rules that will be matched against requests and responses.
Each rule has either a cookie or a header and can be triggered either when the header
or cookie is present or missing.

When a rule is triggered, dynamic metadata will be added based on the configuration of the rule.
If the header or cookie is present, it's value is extracted and used along with the specified
key as metadata. If the header or cookie is missing, on missing case is triggered and the value
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
    :lines: 25-40
    :lineno-start: 25
    :linenos:
    :caption: :download:`header-to-metadata-filter.yaml <_include/header-to-metadata-filter.yaml>`

As with headers, the value of the specified cookie will be extracted from the request
and added as metadata with the key specified.
Removing a cookie when a rule matches is unsupported.

.. literalinclude:: _include/header-to-metadata-filter-cookies.yaml
    :language: yaml
    :lines: 25-40
    :lineno-start: 25
    :linenos:
    :caption: :download:`header-to-metadata-filter-cookies.yaml <_include/header-to-metadata-filter-cookies.yaml>`

A corresponding upstream cluster configuration could be:

.. literalinclude:: _include/header-to-metadata-filter.yaml
    :language: yaml
    :lines: 45-55
    :lineno-start: 45
    :linenos:
    :caption: :download:`header-to-metadata-filter.yaml <_include/header-to-metadata-filter.yaml>`

This would then allow requests with the ``x-version`` header set to be matched against
endpoints with the corresponding version. Whereas requests with that header missing
would be matched with the default endpoints.

If the header's value needs to be transformed before it's added to the request as
dynamic metadata, this filter supports regex matching and substitution:

.. literalinclude:: _include/header-to-metadata-filter-regex-substitution.yaml
    :language: yaml
    :lines: 25-37
    :lineno-start: 25
    :linenos:
    :caption: :download:`header-to-metadata-filter-regex-substitution.yaml <_include/header-to-metadata-filter-regex-substitution.yaml>`

Note that this filter also supports per route configuration:

.. literalinclude:: _include/header-to-metadata-filter-route-config.yaml
    :language: yaml
    :lines: 14-38
    :lineno-start: 14
    :linenos:
    :caption: :download:`header-to-metadata-filter-route-config.yaml <_include/header-to-metadata-filter-route-config.yaml>`

This can be used to either override the global configuration or if the global configuration
is empty (no rules), it can be used to only enable the filter at a per route level.

Statistics
----------

Currently, this filter generates no statistics.
