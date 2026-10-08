Fixed a race where the route level configuration of the
:ref:`AWS request signing <envoy_v3_api_msg_extensions.filters.http.aws_request_signing.v3.AwsRequestSigningPerRoute>`
and :ref:`AWS Lambda <envoy_v3_api_msg_extensions.filters.http.aws_lambda.v3.PerRouteConfig>`
filters could be destroyed on a worker thread when an RDS update replaced the route configuration.
