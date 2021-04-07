
.. _config_http_filters_aws_request_signing:

AWS Request Signing
===================

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning>`
* This filter should be configured with the name *envoy.filters.http.aws_request_signing*.

.. attention::

  The AWS request signing filter is experimental and is currently under active development.

The HTTP AWS request signing filter is used to access authenticated AWS services. It uses the
existing AWS Credential Provider to get the secrets used for generating the required
headers.

The ``unsigned_payload`` option determines whether or not requests are buffered so the request body
can be used to compute the payload hash. Some services, such as S3, allow requests with unsigned
payloads. Consult the AWS documentation and your service's resource policies to determine if this
option is appropriate.

When ``unsigned_payload`` is ``false`` (the default), requests which exceed the configured buffer
limit will receive a 413 response. See the ref:`flow control docs <faq_flow_control>` for details.

Example configuration
---------------------

Example filter configuration:

.. code-block:: yaml

  name: envoy.filters.http.aws_request_signing
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.aws_request_signing.v3.AwsRequestSigning
    service_name: s3
    region: us-west-2
    unsigned_payload: true


Statistics
----------

The AWS request signing filter outputs statistics in the *http.<stat_prefix>.aws_request_signing.* namespace. The
:ref:`stat prefix <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  signing_added, Counter, Total authentication headers added to requests
  signing_failed, Counter, Total requests for which a signature was not added
