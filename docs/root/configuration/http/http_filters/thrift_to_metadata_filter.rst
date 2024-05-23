.. _config_http_filters_thrift_to_metadata:

Envoy Thrift-To-Metadata Filter
===============================
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.thrift_to_metadata.v3.ThriftToMetadata``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.thrift_to_metadata.v3.ThriftToMetadata>`

The Thrift to Metadata filter serves for thrift over HTTP traffic, expecting serialized Thrift request and response bodies
in the HTTP payload. This filter is configured with rules that will be matched against Apache thrift compatible requests and
responses in HTTP payload. The filter will parse the thrift body, extract *thrift metadata* or *thrift payload*, and add them to
*dynamic filter metadata* based on the configuration of the rule.

The *filter metadata* can then be used for load balancing decisions, consumed from logs, etc.

A typical use case for this filter is to dynamically match a specified thrift method of requests
with rate limit. For this, thrift method name is attached to the request as dynamic filter metadata which
would then be used to match a rate limit action on filter metadata.

Example
-------

A sample filter configuration to route traffic to endpoints based on the presence or
absence of a version attribute could be:

.. literalinclude:: _include/thrift-to-metadata-filter.yaml
    :language: yaml
    :lines: 25-55
    :lineno-start: 25
    :linenos:
    :caption: :download:`thrift-to-metadata-filter.yaml <_include/thrift-to-metadata-filter.yaml>`

Statistics
----------

The thrift to metadata filter outputs statistics in the *http.<stat_prefix>.thrift_to_metadata.* namespace. The :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  rq_success, Counter, Total requests that succeed to parse the thrift body.
  rq_mismatched_content_type, Counter, Total requests that mismatch the content type
  rq_no_body, Counter, Total requests without content body
  rq_invalid_thrift_body, Counter, Total requests with invalid thrift body
  resp_success, Counter, Total responses that succeed to parse the thrift body.
  resp_mismatched_content_type, Counter, Total responses that mismatch the content type
  resp_no_body, Counter, Total responses without content body
  resp_invalid_thrift_body, Counter, Total responses with invalid thrift body
