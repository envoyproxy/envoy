.. _config_internal_listener:

Internal Listener
=================

How it works
------------

This extension contains 2 major components to add a listener with
an :ref:`Envoy internal address <envoy_v3_api_msg_config.core.v3.EnvoyInternalAddress>`
and to create a client connection to that :ref:`listener <envoy_v3_api_msg_config.listener.v3.Listener>`

envoy.bootstrap.internal_listener
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This bootstrap extension is required to support looking up the target listener via an
:ref:`envoy internal address <envoy_v3_api_msg_config.core.v3.EnvoyInternalAddress>` on each worker threads.

network.connection.client.envoy_internal
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
It is a client connection factory. The factory is implicitly instantiated by the dispatcher to establish a client connection to an
internal listener address.  This client connection factory is installed automatically when ``envoy.bootstrap.internal_listener`` is specified.

Example config
--------------
Below is a smallest static config that redirect TCP proxy on port 19000 to the TCP proxy binding to the internal address.

.. code-block:: yaml

  static_resources:
    listeners:
    - name: outbound_tcp_svc_19000
      address:
        socket_address:
          address: 0.0.0.0
          port_value: 19000
      filter_chains:
      - filters:
        - name: tcp_proxy
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
            cluster: bridge_internal_listener
            stat_prefix: svc_tcp_proxy
    - name: singleton_internal_encap
      address:
        envoy_internal_address:
          server_listener_name: singleton_internal_encap
      filter_chains:
      - filters:
        - name: tcp_proxy
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
            cluster: singleton_internal_encap
            stat_prefix: encap_tcp_proxy
    clusters:
    - name: bridge_internal_listener
      connect_timeout: 3600s
      type: STATIC
      load_assignment:
        cluster_name: "bridge_internal_listener"
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                envoy_internal_address:
                  server_listener_name: singleton_internal_encap
      transport_socket:
        name: envoy.transport_sockets.raw_buffer
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer
    - name: singleton_internal_encap
      connect_timeout: 3600s
      type: STATIC
      load_assignment:
        cluster_name: "singleton_internal_encap"
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 0.0.0.0
                  port_value: 19001
  bootstrap_extensions:
  - name: envoy.bootstrap.internal_listener
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.bootstrap.internal_listener.v3.InternalListener"
  layered_runtime:
    layers:
    - name: enable_internal_address
      static_layer:
        envoy.reloadable_features.internal_address: true

Real world use cases
--------------------

Encap HTTP GET requests in a HTTP CONNECT request
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Currently Envoy :ref:`HTTP connection manager <config_http_conn_man>`
cannot proxy a GET request in an upstream HTTP CONNECT request. This requirement
can be acomplished by setting up the upstream endpoint of HTTP connection manager to the internal listener address.
Meanwhile, another internal listener binding to the above listener address includes a TCP proxy with :ref:`tunneling config <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.tunneling_config>`.

Decap the CONNECT requests
~~~~~~~~~~~~~~~~~~~~~~~~~~

There are some complicated GET-in-CONNECT requests across services or edges.
In order to proxy the GET request within Envoy, two layer of :ref:`HTTP connection manager <config_http_conn_man>`
is demanded. The first HHTTP connection manager layer extract the TCP stream from a CONNECT request and redirect the TCP stream to the second
HTTP connection manager layer to parse the common GET requests.
