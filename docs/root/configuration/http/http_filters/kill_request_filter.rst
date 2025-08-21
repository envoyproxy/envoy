.. _config_http_filters_kill_request:

Kill Request
============

The KillRequest filter can be used to crash Envoy when receiving a Kill request.
By default, KillRequest filter is not built into Envoy binary. If you want to use this extension,
build Envoy with ``--//source/extensions/filters/http/kill_request:enabled``.

Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.kill_request.v3.KillRequest``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.kill_request.v3.KillRequest>`

.. _config_http_filters_kill_request_http_header:

Enable Kill Request via HTTP header
--------------------------------------------

The KillRequest filter requires a kill header in the request or response. If
*kill_request_header* is not empty in *KillRequest* proto, the name of the kill
header must match *KillRequest.kill_request_header*, otherwise it must match
the default kill header below:

x-envoy-kill-request
  whether the request is a Kill request.
  The header value must be one of (case-insensitive) ["true", "t", "yes", "y", "1"]
  in order for the request to be a Kill request.

.. note::

  If the headers appear multiple times only the first value is used.

The following is an example configuration:

.. code-block:: yaml

  name: envoy.filters.http.kill_request
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.kill_request.v3.KillRequest
    probability:
      numerator: 100
