.. _config_listener_filters_connect_handler:

Connect Handler
===============

Connect Handler listener filter allows detecting whether the incoming request appears to be HTTP/1.x CONNECT, and if it is CONNECT, it removes the CONNECT by reading data from socket and responding with a 200 status code to indicate a connection to remote server is established. It also extracts remote server url from the CONNECT. This can be used to select a
:ref:`FilterChain <envoy_v3_api_msg_config.listener.v3.FilterChain>` via the
:ref:`server_names <envoy_v3_api_field_config.listener.v3.FilterChainMatch.server_names>`. Connect handler is a terminal listener filter which should be the last one in the listener filter list. 

* :ref:`Listener filter v3 API reference <envoy_v3_api_msg_extensions.filters.listener.connect_handler.v3.ConnectHandler>`
* This filter should be configured with the name *envoy.filters.listener.connect_handler*.

Example
-------

A sample filter configuration of connect handler together with other listener filters could be:

.. code-block:: yaml

  listener_filters:
    - name: "envoy.filters.listener.tls_inspector"
    - name: "envoy.filters.listener.connect_handler"

Statistics
----------

This filter has a statistics tree rooted at *connect_handler* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  read_error, Counter, Total read errors
  write_error, Counter, Total write errors
  connect_found, Counter, Total number of times HTTP/1.x CONNECT was found
  connect_not_found, Counter, Total number of times HTTP/1.x CONNECT was not found
