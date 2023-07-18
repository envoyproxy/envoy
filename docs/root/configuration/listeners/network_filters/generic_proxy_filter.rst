.. _config_network_filters_generic_proxy:

Generic proxy
=============


The network filter could be used to add multiple protocols support to Envoy. The community has implemented lots of this kind of
network filters, such as :ref:`Dubbo proxy <config_network_filters_dubbo_proxy>`, :ref:`Thrift proxy <config_network_filters_thrift_proxy>`, etc.
And the developers could also implement their own protocol proxying by creating a new network filter.


However, adding new network filter to support new protocol is not easy. The developers need to implement the codec to parse the binary, to implement the
stream management to handle request/response, to implement the connection management to handle connection lifecycle, etc.
And there are lots of common features like route, L7 filter chain, tracing, metrics, logging, etc. that need to be implemented again and again in each
new network filter.


Another fact is that lots of RPC protocols has a similar architecture, which is request/response based. The request could be routed to different upstream
based on the request properties. For these similar protocols, their network filters also are similar and could be abstracted to following modules:

* Codec: parse the binary to request/response object.
* Stream management: handle the request/response stream.
* Route: route the request to different upstream based on the request properties.
* L7 filter chain: filter the request/response.
* Observability: tracing, metrics, logging, etc.


Except the codec, all other modules are common and could be shared by different protocols. This is the motivation of generic proxy.


Generic proxy is a network filter that could be used to implement new protocol proxying. An extension point is provided to let the users
to configure specific codec. The developers only need to implement the codec to parse the binary and let users to configure the codec in the
generic proxy. The generic proxy will handle the rest of work.


Abstract request/response
-------------------------


Abstraction of the request/response is the core of generic proxy. Different L7 protocols may have different request/response data structure.
But in the generic proxy, except the codec is extented and could be configured by users, other modules are common and shared by different
protocols. So these different request/response data structures of different protocols need to be abstracted and managed in a common abstraction.


An abstract class ``Request`` is defined to represent the request. The ``Request`` provides some virtual methods to get/set the request properties.
The different protocols could extend the ``Request`` and implement the virtual methods to get/set the request properties in their own data structure.
An abstract class ``Response`` is defined to represent the response. It is similar to ``Request``.


Based on the abstraction of the request/response, the generic proxy could handle the request/response stream, route the request to different upstream,
filter the request/response, etc. and need not to know the L7 application and specific data structure of the request/response.


If the developers want to implement a new protocol proxying, they only need to implement the codec to parse the binary data to specific request/response
and ensure they implement ``Request`` or ``Response``. It is much easier than implement a new network filter.


Extendable matcher and route
----------------------------

:ref:`Generic matcher API <arch_overview_matching_api>` is used to construct the route table of the generic proxy. The developers could extend
input and matcher to support new matching logic.

By default, the generic proxy supports following input and matcher:

* :ref:`host <envoy_v3_api_msg_extensions.filters.network.generic_proxy.matcher.v3.HostMatchInput>`: match the host of the request. The host should
  represents set of instances that can be used for load balancing. It could be DNS name or VIP of the target service.
* :ref:`path <envoy_v3_api_msg_extensions.filters.network.generic_proxy.matcher.v3.PathMatchInput>`: match the path of the request. The path should
  represents RPC service name that used to represents set of method or functionality provided by target service.
* :ref:`method <envoy_v3_api_msg_extensions.filters.network.generic_proxy.matcher.v3.MethodMatchInput>`: match the method of the request.
* :ref:`property <envoy_v3_api_msg_extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput>`: match the property of the request.
  The property could be any property of the request that indexed by string key.
* :ref:`request input <envoy_v3_api_msg_extensions.filters.network.generic_proxy.matcher.v3.RequestMatchInput>` and
  :ref:`request matcher <envoy_v3_api_msg_extensions.filters.network.generic_proxy.matcher.v3.RequestMatcher>`: match the whole request by combining
  host, path, method and properties in AND semantics. This is used to match multiple fields of the downstream request and avoid complex combinations of
  host, path, method and properties in the generic matching tree.

.. note::
   Virtual methods are used to get the request properties in ``Request`` like ``host()``, ``path()``, ``method()``, etc. The developers need to
   implement these virtual methods and determine what these fields are in their own protocol. It is possible that a protocol does not have some of
   these fields. For example, the protocol does not have ``host`` field. In this case, the developers could return empty string in the virtual method
   to indicate the field does not exist in the protocol. The only drawback is that the generic proxy could not match the request by this field.


.. literalinclude:: _include/generic_proxy_filter.yaml
    :language: yaml
    :linenos:
    :lineno-start: 21
    :lines: 21-53

Asynchronous codec API
----------------------

The generic proxy provides extension point to let the developers to implement specific codec for their own protocol. The codec API is designed to be
asynchronous to avoid blocking the worker thread. And the asynchronous codec API make it is possible to accelerate the codec by offloading the parsing
work to specific hardware.


Configurable connection
-----------------------

Different protocols may have different connection lifecycle or connection management. The generic proxy provides additional options to let the developers
of codec to configure the connection lifecycle and connection management.


For example, the developers could configure the upstream connection be bound to the downstream connection or not. If the upstream connection is bound to
the downstream connection, the upstream connection will have same lifetime with the downstream connection. And the bound upstream connection will be
used only by the requests come from related downstream connection. This is useful for the protocols that need to keep the connection state.


In addition, the developers could operate the downstream connection and upstream connection in the codec directly. This give the developers more control
of the connection.


Example codec implementation
----------------------------

The community has implemented a :ref:`dubbo codec <envoy_v3_api_msg_extensions.filters.network.generic_proxy.codecs.dubbo.v3.DubboCodecConfig>` based on
generic proxy. The dubbo codec is a good example to show how to implement a new codec for new protocol because its moderate complexity.


You could find the dubbo codec implementation in ``contrib/generic_proxy/filters/network/source/codecs/dubbo`` directory. And you can configure the
dubbo codec in the generic proxy by following configuration:


.. literalinclude:: _include/generic_proxy_filter.yaml
    :language: yaml
    :linenos:
