
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

This filter can be used to communicate with both AWS API endpoints and customer API endpoints, such as AWS API Gateway
hosted APIs or Amazon VPC Lattice services.

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

The :ref:`signing_algorithm <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.signing_algorithm>`
can be specified as ``AWS_SIGV4`` or ``AWS_SIGV4A``. If the signing algorithm is unspecified, this filter will default to ``AWS_SIGV4``.
If ``AWS_SIGV4`` is unspecified, or explicitly specified, the :ref:`signing_algorithm <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.region>` parameter
is used to define the region to which the sigv4 calculation is addressed to.
If ``AWS_SIGV4A`` is explicitly specified, the :ref:`signing_algorithm <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.region>` parameter
is used as a region set. A region set is a single region, or comma seperated list of regions. Regions in a region set can also include wildcards,
such as ``us-east-*`` or even ``*``. By using ``AWS_SIGV4A`` and wildcarded regions it is possible to simplify the overall envoy configuration for
multi-region implementations.

Signing can be added to the query string, rather than in the headers, by enabling :ref:`query_string <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.query_string>`
Query string signing adds an additional parameter :ref:`expiration_time <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.QueryString.expiration_time>` which determines the
length of time after which this URL becomes invalid, starting from the time the URL is signed.
The default expiration time is 5 seconds, with a maximum of 3600 seconds. It is recommended to keep this value as small as practicable,
as the generated URL is replayable before this time expires.

Header Modification
-------------------

Unless the  :ref:`query_string <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.query_string>` signing method is used,
the following HTTP header modifications will be made by this extension:
- The HTTP ``authorization`` header will be replaced with the calculated SigV4/SigV4A Authorization value
- The ``x-amz-security-token`` header will be removed, or replaced if a session token is present via credentials
- The ``x-amz-date`` header will be replaced with the current date
- The ``x-amz-region-set`` header will replaced if the ``AWS_SIGV4A`` signing algorithm is used

Example configuration
---------------------

Example filter configuration:

.. literalinclude:: _include/aws-request-signing-filter.yaml
    :language: yaml
    :lines: 25-35
    :lineno-start: 25
    :linenos:
    :caption: :download:`aws-request-signing-filter.yaml <_include/aws-request-signing-filter.yaml>`

This filter also supports per route configuration. Below is an example of route-level config overriding the config at the virtual-host level.

.. literalinclude:: _include/aws-request-signing-filter-route-level-override.yaml
    :language: yaml
    :lines: 20-37
    :lineno-start: 20
    :linenos:
    :caption: :download:`aws-request-signing-filter-route-level-override.yaml <_include/aws-request-signing-filter-route-level-override.yaml>`

An example of configuring this filter to use ``AWS_SIGV4A`` signing with a wildcarded region set, to an Amazon VPC Lattice service, using query string signing and a 3 second expiration:

.. literalinclude:: _include/aws-request-signing-filter-sigv4a.yaml
    :language: yaml
    :lines: 26-36
    :lineno-start: 26
    :linenos:
    :caption: :download:`aws-request-signing-filter-sigv4a.yaml <_include/aws-request-signing-filter-sigv4a.yaml>`


Configuration as an upstream HTTP filter
----------------------------------------
SigV4 or SigV4A request signatures are calculated using the HTTP host, URL and payload as input. Depending on the configuration, Envoy may modify one or more of
these prior to forwarding to the Cluster subsystem, but after the signature has been calculated and inserted into the HTTP headers or query string.
Modifying fields in a SigV4 or SigV4A signed request will result in an invalid signature.

To avoid invalid signatures, the AWS Request Signing Filter can be configured as an upstream HTTP filter. This allows signatures to be
calculated as a final step before the HTTP request is forwarded upstream, ensuring signatures are correctly calculated over the updated
HTTP fields.

Configuring this filter as an upstream HTTP filter is done in a similar way to the downstream case, but using the :ref:`http_filters <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.http_filters>`
filter chain within the cluster configuration.

.. literalinclude:: _include/aws-request-signing-filter-upstream.yaml
    :language: yaml
    :lines: 47-57
    :lineno-start: 47
    :linenos:
    :caption: :download:`aws-request-signing-filter-upstream.yaml <_include/aws-request-signing-filter-upstream.yaml>`

.. include:: _include/aws_credentials.rst

.. _config_http_filters_aws_request_signing_region:

.. include:: _include/aws_regions.rst

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

