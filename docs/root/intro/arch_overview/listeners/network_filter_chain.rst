.. _arch_overview_network_filter_chain:

Network Filter Chain
====================

As discussed in the :ref:`listener <arch_overview_listeners>` section, network level (L3/L4) filters
form the core of Envoy connection handling.

The network filters are chained in a ordered list known as :ref:`filter chain <envoy_v3_api_msg_config.listener.v3.FilterChain>`. 
Each listener has multiple filter chains and an optional default filter chain.When a connection is accepted, the
listener picks the best filter chain according to the :ref:`FilterChainMatch <envoy_v3_api_msg_config.listener.v3.FilterChainMatch>`
associated with each filter chain. If the best match filter chain cannot be found, the default filter chain will be
choosen to serve the request. If the default filter chain is not supplied, the connection will be closed.


Filter chain only update
------------------------

:ref:`Filter chain <envoy_v3_api_msg_config.listener.v3.FilterChain>` can be updated indepedently. Upon listener config
update, if the listener manager determines that the listener update is filter chain only update, the listener update
will be executed by adding, updating and removing filter chains. The connections owned by destroying filter chain will
be drained as describe in listener drain. 

If the new :ref:`filter chain <envoy_v3_api_msg_config.listener.v3.FilterChain>` and the old :ref:`filter chain <envoy_v3_api_msg_config.listener.v3.FilterChain>`
is protobuf message equivalent, the corresponding filter chain runtime info survives. The connections owned by the
survived filter chains remain open.

Not all the listener config updates can be executed by filter chain update. For example, if the listener metadata is
updated within the new listener config, the new metadata must be picked up by the new filter chains. In this case, the
entire listener is drained and updated.