.. _config_network_filters_generic_proxy:

Generic proxy
=============


The network filter could be used to add multiple protocols support to Envoy. The community has implemented lots of this kind of
network filter, such as :ref:`Dubbo proxy <config_network_filters_dubbo_proxy>`, :ref:`Thrift proxy <config_network_filters_thrift_proxy>`, etc.
Developers may also implement their own protocol proxying by creating a new network filter.


Adding a new network filter to support a new protocol is not easy. Developers need to implement the codec to parse the binary, to implement the
stream management to handle request/response, to implement the connection management to handle connection lifecycle, etc.
There are many common features like route, L7 filter chain, tracing, metrics, logging, etc. that need to be implemented again and again in each
new network filter.


Many RPC protocols has a similar request/response based architecture. The request could be routed to different upstreams
based on the request properties. For these similar protocols, their network filters also are similar and could be abstracted to following modules:

* Codec: parse the binary to request/response object.
* Stream management: handle the request/response stream.
* Route: route the request to different upstream based on the request properties.
* L7 filter chain: filter the request/response.
* Observability: tracing, metrics, logging, etc.


With the exception of the codec, all other modules are common and could be shared by different protocols. This is the motivation of generic proxy.


Generic proxy is a network filter that could be used to implement new protocol proxying. An extension point is provided to let the users
configure specific codec. The developers only need to implement the codec to parse the binary and let users configure the codec in the
generic proxy. The generic proxy will handle the rest of work.


Abstract request/response
-------------------------


Abstraction of the request/response is the core of generic proxy. Different L7 protocols may have a different request/response data structure.
In the generic proxy, the codec is extended and could be configured by users, but other modules are common and can be shared by different
protocols. These different request/response data structures of different protocols need to be abstracted and managed in a common abstraction.


An abstract ``Request`` class is defined to represent the request. The ``Request`` provides some virtual methods to get/set the request properties.
Different protocols can extend the ``Request`` and implement the virtual methods to get/set the request properties in their own data structure.
An abstract class ``Response`` is defined to represent the response. It is similar to ``Request``.


Based on the abstraction of the request/response, the generic proxy can handle the request/response stream, route the request to different upstreams,
filter the request/response, etc. without needing to know the L7 application and specific data structure of the request/response.


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

The generic proxy provides an extension point to let the developers implement a codec specific to their own protocol. The codec API is designed to be
asynchronous to avoid blocking the worker thread. And the asynchronous codec API makes it possible to accelerate the codec by offloading the parsing
work to specific hardware.


Configurable connection
-----------------------

Different protocols may have different connection lifecycles or connection management. The generic proxy provides additional options to let codec developers configure the connection lifecycle and connection management.


For example, developers can configure whether the upstream connection is bound to the downstream connection or not. If the upstream connection is bound to
the downstream connection, the upstream connection will have same lifetime as the downstream connection. The bound upstream connection will be
used only by requests that come from the related downstream connection. This is useful for the protocols that need to keep the connection state.


Developers can also operate the downstream connection and upstream connection in the codec directly. This gives developers more control
over the connection.


Example codec implementation
----------------------------

The community has implemented a :ref:`dubbo codec <envoy_v3_api_msg_extensions.filters.network.generic_proxy.codecs.dubbo.v3.DubboCodecConfig>` based on
generic proxy. The dubbo codec is a good example showing how to implement a new codec for new protocol because of its moderate complexity.


You could find the dubbo codec implementation in :repo:`source/extensions/filters/network/generic_proxy/codecs/dubbo` directory. You can also configure the
dubbo codec in the generic proxy with the following configuration:


.. literalinclude:: _include/generic_proxy_filter.yaml
    :language: yaml
    :linenos:


Access logging support
----------------------

The generic proxy supports access logging. The developers could configure the access log format and access log path in the
file access log configuration like the HTTP connection manager. Here is an example of the access log configuration:


.. literalinclude:: _include/generic_proxy_filter.yaml
    :language: yaml
    :linenos:
    :lineno-start: 55
    :lines: 55-62


All protocol independent fields (like ``%RESPONSE_FLAGS%``, ``%RESPONSE_CODE%``, ``%RESPONSE_CODE_DETAILS%``, ``%DURATION%``, etc.)
are supported in the access log format. In addition, the generic proxy also supports following generic proxy specific fields:

* ``%METHOD%``: The method of generic request. The meaning of the return value may be different for different application protocols.
* ``%PATH%``: The path of generic request. The meaning of the return value may be different for different application protocols.
* ``%HOST%``: The host of generic request. The meaning of the return value may be different for different application protocols.
* ``%PROTOCOL%``: The application protocol name.
* ``%REQUEST_PROPERTY(X)%``: The request property of generic request. The ``X`` is the property name. The value value depends on the
  application codec implementation.
* ``%RESPONSE_PROPERTY(X)%``: The response property of generic response. The ``X`` is the property name. The value value depends on the
  application codec implementation.


Generic proxy statistics
------------------------

The generic proxy provides some statistics to help users understand the generic proxy behavior. Here are all the generic proxy statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   ``downstream_rq_total``, Counter, Total requests
   ``downstream_rq_error``, Counter, Total requests that with non-OK response status
   ``downstream_rq_reset``, Counter, Total requests that are reset before response
   ``downstream_rq_local``, Counter, Total requests that with local response
   ``downstream_rq_decoding_error``, Counter, Total codec decoding errors
   ``downstream_rq_active``, Gauge, Current active requests
   ``downstream_rq_time``, Histogram, Request time in milliseconds. The time is measured from when the request is received from downstream to when the response is sent to downstream
   ``downstream_rq_tx_time``, Histogram, Request time in microseconds. The time is measured from when the request is received from downstream to the request is sent to upstream
   ``downstream_rq_code_XXX``, Counter, Total requests that with response status code XXX. The XXX is the response status code. For example the ``downstream_rq_code_200`` is the total requests that with response status code 200. Note only response code between 0 and 999 are supported
   ``downstream_rq_flag_XXX``, Counter, Total requests that with response flag XXX. The XXX is the response flag. For example the ``downstream_rq_flag_UF`` is the total requests that with response flag UF
