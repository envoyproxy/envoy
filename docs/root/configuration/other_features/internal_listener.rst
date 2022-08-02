.. _config_internal_listener:

Internal Listener
=================

Envoy supports user-space sockets that enable establishing TCP streams from an upstream cluster to a listener without
using the system network API. A listener that accepts user space connections is called an _internal listener_. The
internal listener :ref:`name <envoy_v3_api_field_config.listener.v3.Listener.name>` identifies the server for a client
:ref:`internal address <envoy_v3_api_msg_config.core.v3.EnvoyInternalAddress>`.

.. note::
  Internal listeners require :ref:`the bootstrap extension
  <envoy_v3_api_msg_extensions.bootstrap.internal_listener.v3.InternalListener>` to be enabled.

Internal upstream transport
---------------------------

:ref:`Internal upstream transport
<envoy_v3_api_msg_extensions.transport_sockets.internal_upstream.v3.InternalUpstreamTransport>`
extension enables exchange of the filter state from the downstream listener to
the internal listener through a user space socket. This additional state can be
in the form of the resource metadata obtained from the upstream host or
:ref:`the filter state objects <arch_overview_data_sharing_between_filters>`.

This extension emits the following statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   no_metadata, Counter, Metadata key is absent from the import location.

Examples
--------

Simple chaining
~~~~~~~~~~~~~~~

A minimal example that chains two TCP proxies to forward connections from port 9999 to port 10000 via an internal
listener can be found :repo:`here <configs/internal_listener_proxy.yaml>`

Encapsulate HTTP GET requests in a HTTP CONNECT request
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Currently Envoy :ref:`HTTP connection manager <config_http_conn_man>`
cannot proxy a GET request in an upstream HTTP CONNECT request. This requirement
can be accomplished by setting up the upstream endpoint of HTTP connection manager to the internal listener address.
Meanwhile, another internal listener binding to the above listener address includes a TCP proxy with :ref:`tunneling config <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.tunneling_config>`.

A sample config can be found :repo:`here <configs/encapsulate_http_in_http2_connect.yaml>`

Decapsulate the CONNECT requests
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are some complicated GET-in-CONNECT requests across services or edges.
In order to proxy the GET request within Envoy, two layer of :ref:`HTTP connection manager <config_http_conn_man>`
is demanded. The first HHTTP connection manager layer extract the TCP stream from a CONNECT request and redirect the TCP stream to the second
HTTP connection manager layer to parse the common GET requests.

A sample config can be found :repo:`here <configs/terminate_http_in_http2_connect.yaml>`

The above two examples can be tested together as follows:

* ``bazel-bin/source/exe/envoy-static --config-path configs/encapsulate_http_in_http2_connect.yaml --disable-hot-restart``
* ``bazel-bin/source/exe/envoy-static --config-path configs/terminate_http_in_http2_connect.yaml --disable-hot-restart``.
* ``curl 127.0.0.1:10000``
