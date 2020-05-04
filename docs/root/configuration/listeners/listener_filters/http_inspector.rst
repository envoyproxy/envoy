.. _config_listener_filters_http_inspector:

HTTP Inspector
==============

HTTP Inspector listener filter allows detecting whether the application protocol appears to be HTTP, 
and if it is HTTP, it detects the HTTP protocol (HTTP/1.x or HTTP/2) further. This can be used to select a
:ref:`FilterChain <envoy_v3_api_msg_config.listener.v3.FilterChain>` via the :ref:`application_protocols <envoy_v3_api_field_config.listener.v3.FilterChainMatch.application_protocols>`
of a :ref:`FilterChainMatch <envoy_v3_api_msg_config.listener.v3.FilterChainMatch>`.

* :ref:`Listener filter v3 API reference <envoy_v3_api_msg_extensions.filters.listener.http_inspector.v3.HttpInspector>`
* This filter should be configured with the name *envoy.filters.listener.http_inspector*.

Example
-------

A sample filter configuration could be:

.. code-block:: yaml

  listener_filters:
    - name: "envoy.filters.listener.http_inspector"
      typed_config: {}

Statistics
----------

This filter has a statistics tree rooted at *http_inspector* with the following statistics: 

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  read_error, Counter, Total read errors
  http10_found, Counter, Total number of times HTTP/1.0 was found
  http11_found, Counter, Total number of times HTTP/1.1 was found
  http2_found, Counter, Total number of times HTTP/2 was found
  http_not_found, Counter, Total number of times HTTP protocol was not found
