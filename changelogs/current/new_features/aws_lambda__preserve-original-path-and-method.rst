Added :ref:`preserve_original_path_and_method
<envoy_v3_api_field_extensions.filters.http.aws_lambda.v3.Config.preserve_original_path_and_method>`
to the :ref:`AWS Lambda filter <config_http_filters_aws_lambda>`. When enabled, the filter records
the request's original ``:path`` and ``:method`` into ``x-envoy-original-path`` and
``x-envoy-original-method`` before rewriting them to the Lambda Invoke API target, so access logs
and the ``http.url`` span attribute report the downstream path rather than the Invoke API path.
Defaults to false, leaving existing behaviour unchanged.
