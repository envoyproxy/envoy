.. _best_practices_level2:

Configuring Envoy as a level two proxy
======================================

Envoy is a production-ready proxy, however, the default settings that are tailored for the
edge use case may need to be adjusted when using Envoy in a multi-level deployment as a
"level two" proxy.

.. image:: /_static/multilevel_deployment.svg

**In summary, if you run level two Envoy version 1.11.1 or greater which terminates
HTTP/2 or above, we strongly advise you to change the HttpConnectionManager configuration of your level
two Envoy, by setting its downstream**
:ref:`validation of HTTP messaging option <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stream_error_on_invalid_http_message>`
**to true.**

If there is an invalid request and this option is not set, the Envoy in
question will reset the entire connection. This behavior was changed as part of
the 1.11.1 security release, to increase the security of Edge Envoys. Unfortunately,
because there are no guarantees that edge proxies will enforce HTTP
standards compliance as rigorously as Envoy’s stack does, this can result
in a problem as follows. If one client sends a request that for example passes
level one proxy's validation checks, and it is forwarded over an upstream multiplexed
connection (potentially shared with other clients) the strict enforcement on
the level two Envoy will reset all the streams on that connection, causing
a service disruption to the clients sharing that L1-L2 connection. If a malicious
user has insight into what traffic will bypass level one checks, they could spray
“bad” traffic across the level one fleet, causing serious disruption to other users’
traffic.

This configuration option also has implications for invalid HTTP/1.1 though slightly less
severe ones. For Envoy L1s, invalid HTTP/1 requests will also result in connection
reset. If the option is set to true, and the request is completely read, the connection
will persist and can be reused for a subsequent request.
