
.. _config_http_filters_aws_request_signing:

AWS Request Signing
===================

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.aws_request_signing.v3.AwsRequestSigning``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning>`

.. attention::

  The AWS request signing filter is experimental and is currently under active development.

The HTTP AWS request signing filter is used to access authenticated AWS services. It uses the
existing AWS Credential Provider to get the secrets used for generating the required
headers.


The :ref:`use_unsigned_payload <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.use_unsigned_payload>`
option determines whether or not requests are buffered so the request body can be used to compute the payload hash. Some
services, such as S3, allow requests with unsigned payloads. Consult the AWS documentation and your service's resource
policies to determine if this option is appropriate.

When :ref:`use_unsigned_payload <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.use_unsigned_payload>`
is false (the default), requests which exceed the configured buffer limit will receive a 413 response. See the
:ref:`flow control docs <faq_flow_control>` for details.

The :ref:`match_excluded_headers <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.match_excluded_headers>`
option allows excluding certain request headers from being signed. This usually applies to headers that are likely to mutate or
are added later such as in retries. By default, the headers ``x-forwarded-for``, ``x-forwarded-proto``, and ``x-amzn-trace-id`` are always excluded.

Example configuration
---------------------

Example filter configuration:

.. literalinclude:: _include/aws-request-signing-filter.yaml
    :language: yaml
    :lines: 25-35
    :lineno-start: 25
    :linenos:
    :caption: :download:`aws-request-signing-filter.yaml <_include/aws-request-signing-filter.yaml>`

Note that this filter also supports per route configuration:

.. literalinclude:: _include/aws-request-signing-filter-route-level-override.yaml
    :language: yaml
    :lines: 20-37
    :lineno-start: 20
    :linenos:
    :caption: :download:`aws-request-signing-filter-route-level-override.yaml <_include/aws-request-signing-filter-route-level-override.yaml>`

Above shows an example of route-level config overriding the config on the virtual-host level.

.. include:: _include/aws_credentials.rst

Statistics
----------

The AWS request signing filter outputs statistics in the *http.<stat_prefix>.aws_request_signing.* namespace. The
:ref:`stat prefix <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  signing_added, Counter, Total requests for which signing succeeded (includes payload_signing_added)
  signing_failed, Counter, Total requests for which signing failed (includes payload_signing_failed)
  payload_signing_added, Counter, Total requests for which the payload was buffered signing succeeded
  payload_signing_failed, Counter, Total requests for which the payload was buffered but signing failed
