
.. _config_http_filters_aws_request_signing:

AWS Request Signing
===================

* :ref:`v2 API reference <envoy_api_msg_config.filter.http.aws_request_signing.v2alpha.AwsRequestSigning>`
* This filter should be configured with the name *envoy.filters.http.aws_request_signing*.

.. attention::

  The AWS request signing filter is experimental and is currently under active development.

The HTTP AWS request signing filter is used to access authenticated AWS services. It uses the
existing AWS Credential Provider to get the secrets used for generating the required
headers.

Example configuration
---------------------

Example filter configuration:

.. code-block:: yaml

  name: envoy.filters.http.aws_request_signing
  typed_config:
    "@type": type.googleapis.com/envoy.config.filter.http.aws_request_signing.v2alpha.AwsRequestSigning
    service_name: s3
    region: us-west-2


Statistics
----------

The AWS request signing filter outputs statistics in the *http.<stat_prefix>.aws_request_signing.* namespace. The
:ref:`stat prefix <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  signing_added, Counter, Total authentication headers added to requests
  signing_failed, Counter, Total requests for which a signature was not added
