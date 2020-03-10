
.. _config_http_filters_aws_lambda:

AWS Lambda
==========

* :ref:`v2 API reference <envoy_api_msg_config.filter.http.aws_lambda.v2alpha.config>`
* This filter should be configured with the name *envoy.filters.http.aws_lambda*.

.. attention::

  The AWS Lambda filter is currently under active development.

The HTTP AWS Lambda filter is used to trigger an AWS Lambda function from a standard HTTP/1.x or HTTP/2 request.
It supports a few options to control whether to pass through the HTTP request payload as is or to wrap it in a JSON
schema.

If :ref:`payload_passthrough <envoy_api_msg_config.filter.http.aws_lambda.v2alpha.config>` is set to
``true``, then the payload is sent to Lambda without any transformations.
*Note*: This means you lose access to all the HTTP headers in the Lambda function.

However, if :ref:`payload_passthrough <envoy_api_msg_config.filter.http.aws_lambda.v2alpha.config>`
is set to ``false``, then the HTTP request is transformed to a JSON (the details of the JSON transformation will be
documented once that feature is implemented).

The filter supports :ref:`per-filter configuration
<envoy_api_msg_config.filter.http.aws_lambda.v2alpha.PerRouteConfig>`.
Below are some examples the show how the filter can be used in different deployment scenarios.

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
        "@type": type.googleapis.com/envoy.api.v2.auth.UpstreamTlsContext
        sni: "*.amazonaws.com"


The filter can also be configured per virtual-host, route or weighted-cluster. In that case, the target cluster *must*
have specific Lambda metadata.

.. code-block:: yaml

    weighted_clusters:
    clusters:
    - name: lambda_egress_gateway
      weight: 42
      typed_per_filter_config:
        envoy.filters.http.aws_lambda:
          "@type": type.googleapis.com/envoy.extensions.filters.http.aws_lambda.v3.PerRouteConfig
          invoke_config:
            arn: "arn:aws:lambda:us-west-2:987654321:function:hello_envoy"
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
                address: lambda.us-west-2.amazonaws.com
                port_value: 443
    transport_socket:
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.api.v2.auth.UpstreamTlsContext
        sni: "*.amazonaws.com"

