.. _config_http_filters_transform:

Transform
=========

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.transform.v3.TransformConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.transform.v3.TransformConfig>`

This filter can be used to transform HTTP requests and responses with
:ref:`substitution format string <envoy_v3_api_msg_config.core.v3.SubstitutionFormatString>`. For example, it can be used to:

* Modify request or response headers based on values in the body and refresh the routes accordingly.
* Modify JSON request or response bodies.

Configuration
-------------

The following example configuration will extract the ``model`` field from a JSON request body
and add it as a request header ``model-header`` before forwarding the request to the upstream.
At the same time, it will also rewrite the ``model`` field in the JSON response body as ``new-model``.

At the response path, the filter is configured to extract the ``completion_tokens`` and ``prompt_tokens``
fields from the JSON response body and add them as response headers.

.. literalinclude:: _include/transform_filter.yaml
    :language: yaml
    :lines: 42-69
    :lineno-start: 42
    :linenos:
    :caption: :download:`transform_filter.yaml <_include/transform_filter.yaml>`

Per-route configuration
-----------------------

Per-route overrides may be supplied via the same protobuf API in the ``typed_per_filter_config``
field of route configuration.

The following example configuration will override the global filter configuration to keep
only the request headers transformation.

.. literalinclude:: _include/transform_filter.yaml
    :language: yaml
    :lines: 26-35
    :lineno-start: 26
    :linenos:
    :caption: :download:`transform_filter.yaml <_include/transform_filter.yaml>`

Enhanced substitution format
----------------------------

The :ref:`substitution format specifier <config_access_log_format>` could be used for both
headers and body transformations.

And except the commonly used format specifiers, there are some additional format specifiers
provided by the transform filter:

* ``%REQUEST_BODY(KEY*)%``: the request body. And ``Key`` KEY is an optional
  lookup key in the namespace with the option of specifying nested keys separated by ':'.
* ``%RESPONSE_BODY(KEY*)%``: the response body. And ``Key`` KEY is an optional
  lookup key in the namespace with the option of specifying nested keys separated by ':'.
