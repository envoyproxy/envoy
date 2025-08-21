.. _config_http_filters_json_to_metadata:

Envoy Json-To-Metadata Filter
=============================
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.json_to_metadata.v3.JsonToMetadata``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.json_to_metadata.v3.JsonToMetadata>`

This filter is configured with rules that will be matched against requests and responses.
Each rule has either a series of selectors and can be triggered either when the json attribute
is present or missing.

When a rule is triggered, dynamic metadata will be added based on the configuration of the rule.
If present, it's value is extracted and used along with the specified key as metadata. If missing,
on missing case is triggered and the value specified is used for adding metadata.

The metadata can then be used for load balancing decisions, consumed from logs, etc.

A typical use case for this filter is to dynamically match a specified json attribute of requests
with rate limit. For this, a given value of the JSON keys would be extracted and attached
to the request as dynamic metadata which would then be used to match a rate limit action on metadata.

The Json to metadata filter stops iterating to next filter until we have the whole payload to extract
the Json attributes or encounter error.

Example
-------

A sample filter configuration to route traffic to endpoints based on the presence or
absence of a version attribute could be:

.. literalinclude:: _include/json-to-metadata-filter.yaml
    :language: yaml
    :lines: 25-45
    :lineno-start: 25
    :linenos:
    :caption: :download:`json-to-metadata-filter.yaml <_include/json-to-metadata-filter.yaml>`

Statistics
----------

The json to metadata filter outputs statistics in the *http.<stat_prefix>.json_to_metadata.* namespace. The :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  rq_success, Counter, Total requests that succeed to parse the json body. Note that a pure string or number body is treated as a successful json body which increases this counter.
  rq_mismatched_content_type, Counter, Total requests that mismatch the content type
  rq_no_body, Counter, Total requests without content body
  rq_invalid_json_body, Counter, Total requests with invalid json body
  resp_success, Counter, Total responses that succeed to parse the json body. Note that a pure string or number body is treated as a successful json body which increases this counter.
  resp_mismatched_content_type, Counter, Total responses that mismatch the content type
  resp_no_body, Counter, Total responses without content body
  resp_invalid_json_body, Counter, Total responses with invalid json body
