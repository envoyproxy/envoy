.. _config_internal_listener:

Internal Listener
=================

How it works
------------

 This extension contains 2 major components to add a listener with
 an :ref:`envoy internal address <envoy_v3_api_msg_config.core.v3.EnvoyInternalAddress>`
 and to create a client connection to that :ref:`listener <envoy_v3_api_msg_config.listener.v3.Listener>`

 1. The bootstrap extension `envoy.bootstrap.internal_listener_registry`

 This bootstrap extension is required to support looking up the target listener via an
 :ref:`envoy internal address <envoy_v3_api_msg_config.core.v3.EnvoyInternalAddress>` on each worker threads.

 2. The client connection factory `network.connection.client.envoy_internal`

 This factory is implicitly instantiated by the dispatcher to establish a client connection to the above
 internal listener.
