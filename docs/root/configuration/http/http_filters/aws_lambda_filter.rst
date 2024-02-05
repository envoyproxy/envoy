
.. _config_http_filters_aws_lambda:

AWS Lambda
==========

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.aws_lambda.v3.Config``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.aws_lambda.v3.Config>`

.. attention::

  The AWS Lambda filter is currently under active development.

The HTTP AWS Lambda filter is used to trigger an AWS Lambda function from a standard HTTP request.
It supports a few options to control whether to pass through the HTTP request payload as is or to wrap it in a JSON
schema.

If :ref:`payload_passthrough <envoy_v3_api_field_extensions.filters.http.aws_lambda.v3.Config.payload_passthrough>` is set to
``true``, then the payload is sent to Lambda without any transformations.
*Note*: This means you lose access to all the HTTP headers in the Lambda function.

However, if :ref:`payload_passthrough <envoy_v3_api_field_extensions.filters.http.aws_lambda.v3.Config.payload_passthrough>`
is set to ``false``, then the HTTP request is transformed to a JSON payload with the following schema:

.. code-block:: json
   :force:

    {
        "raw_path": "/path/to/resource",
        "method": "GET|POST|HEAD|...",
        "headers": {"header-key": "header-value", ... },
        "query_string_parameters": {"key": "value", ...},
        "body": "...",
        "is_base64_encoded": true|false
    }

- ``raw_path`` is the HTTP request resource path (including the query string)
- ``method`` is the HTTP request method. For example ``GET``, ``PUT``, etc.
- ``headers`` are the HTTP request headers. If multiple headers share the same name, their values are
  coalesced into a single comma-separated value.
- ``query_string_parameters`` are the HTTP request query string parameters. If multiple parameters share the same name,
  the last one wins. That is, parameters are **not** coalesced into a single value if they share the same key name.
- ``body`` the body of the HTTP request is base64-encoded by the filter if the ``content-type`` header exists and is **not** one of the following:

    -  text/*
    -  application/json
    -  application/xml
    -  application/javascript

Otherwise, the body of HTTP request is added to the JSON payload as is.

On the other end, the response of the Lambda function must conform to the following schema:

.. code-block:: json
   :force:

    {
        "status_code": ...
        "headers": {"header-key": "header-value", ... },
        "cookies": ["key1=value1; HttpOnly; ...", "key2=value2; Secure; ...", ...],
        "body": "...",
        "is_base64_encoded": true|false
    }

- The ``status_code`` field is an integer used as the HTTP response code. If this key is missing, Envoy returns a ``200
  OK``.
- The ``headers`` are used as the HTTP response headers.
- The ``cookies`` are used as ``Set-Cookie`` response headers. Unlike the request headers, cookies are _not_ part of the
  response headers because the ``Set-Cookie`` header cannot contain more than one value per the `RFC`_. Therefore, each
  key/value pair in this JSON array will translate to a single ``Set-Cookie`` header.
- The ``body`` is base64-decoded if it is marked as base64-encoded and sent as the body of the HTTP response.

.. _RFC: https://tools.ietf.org/html/rfc6265#section-4.1

.. note::

    The target cluster must have its endpoint set to the `regional Lambda endpoint`_. Use the same region as the Lambda
    function.

    AWS IAM credentials must be defined in either environment variables, EC2 metadata or ECS task metadata.


.. _regional Lambda endpoint: https://docs.aws.amazon.com/general/latest/gr/lambda-service.html

The filter supports :ref:`per-filter configuration
<envoy_v3_api_msg_extensions.filters.http.aws_lambda.v3.PerRouteConfig>`.

If you use the per-filter configuration, the target cluster *must* have the following metadata:

.. code-block:: yaml

    metadata:
      filter_metadata:
        com.amazonaws.lambda:
          egress_gateway: true


Below are some examples that show how the filter can be used in different deployment scenarios.

Example configuration
---------------------

In this configuration, the filter applies to all routes in the filter chain of the http connection manager:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.aws_lambda
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.aws_lambda.v3.Config
      arn: "arn:aws:lambda:us-west-2:987654321:function:hello_envoy"
      payload_passthrough: true

The corresponding regional endpoint must be specified in the target cluster. So, for example if the Lambda function is
in us-west-2:

.. code-block:: yaml

  clusters:
  - name: lambda_egress_gateway
    connect_timeout: 0.25s
    type: LOGICAL_DNS
    dns_lookup_family: V4_ONLY
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: lambda_egress_gateway
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: lambda.us-west-2.amazonaws.com
                port_value: 443
    transport_socket:
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
        sni: "*.amazonaws.com"


The filter can also be configured per virtual-host, route or weighted-cluster. In that case, the target cluster *must*
have specific Lambda metadata and target cluster's endpoint should point to a region where the Lambda function is present.


.. code-block:: yaml

    weighted_clusters:
    clusters:
    - name: lambda_egress_gateway
      weight: 42
      typed_per_filter_config:
        envoy.filters.http.aws_lambda:
          "@type": type.googleapis.com/envoy.extensions.filters.http.aws_lambda.v3.PerRouteConfig
          invoke_config:
            arn: "arn:aws:lambda:us-west-1:987654321:function:hello_envoy"
            payload_passthrough: false


An example with the Lambda metadata applied to a weighted-cluster:

.. code-block:: yaml

  clusters:
  - name: lambda_egress_gateway
    connect_timeout: 0.25s
    type: LOGICAL_DNS
    dns_lookup_family: V4_ONLY
    lb_policy: ROUND_ROBIN
    metadata:
      filter_metadata:
        com.amazonaws.lambda:
          egress_gateway: true
    load_assignment:
      cluster_name: lambda_egress_gateway # does this have to match? seems redundant
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: lambda.us-west-1.amazonaws.com
                port_value: 443
    transport_socket:
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
        sni: "*.amazonaws.com"


.. include:: _include/aws_credentials.rst

Statistics
----------

The AWS Lambda filter outputs statistics in the *http.<stat_prefix>.aws_lambda.* namespace. The
| :ref:`stat prefix <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  server_error, Counter, Total requests that returned invalid JSON response (see :ref:`payload_passthrough <envoy_v3_api_msg_extensions.filters.http.aws_lambda.v3.Config>`)
  upstream_rq_payload_size, Histogram, Size in bytes of the request after JSON-transformation (if any).
