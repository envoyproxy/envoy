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

Statistics
----------

The filter can optionally emit statistics when the :ref:`stat_prefix <envoy_v3_api_field_extensions.filters.http.header_to_metadata.v3.Config.stat_prefix>` field is configured.

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.header_to_metadata
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.header_to_metadata.v3.Config
      stat_prefix: header_converter
      request_rules:
      - header: x-version
        on_header_present:
          metadata_namespace: envoy.lb
          key: version
          type: STRING

This configuration would emit statistics such as:

- ``http_filter_name.header_converter.request_rules_processed``
- ``http_filter_name.header_converter.request_metadata_added``
- ``http_filter_name.header_converter.response_rules_processed``
- ``http_filter_name.header_converter.response_metadata_added``
- ``http_filter_name.header_converter.request_header_not_found``

When ``stat_prefix`` is not configured, no statistics are emitted.

These statistics are rooted at *http_filter_name.<stat_prefix>* with the following counters:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  request_rules_processed, Counter, Total number of request rules processed
  response_rules_processed, Counter, Total number of response rules processed
  request_metadata_added, Counter, Total number of metadata entries successfully added from request headers
  response_metadata_added, Counter, Total number of metadata entries successfully added from response headers
  request_header_not_found, Counter, Total number of times expected request headers were missing
  response_header_not_found, Counter, Total number of times expected response headers were missing
  base64_decode_failed, Counter, Total number of times Base64 decoding failed
  header_value_too_long, Counter, Total number of times header values exceeded the maximum length
  regex_substitution_failed, Counter, Total number of times regex substitution resulted in empty values

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
