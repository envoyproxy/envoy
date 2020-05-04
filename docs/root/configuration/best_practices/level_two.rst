.. _best_practices_level2:

Configuring Envoy as a level two proxy
======================================

Envoy is a production-ready proxy, however, the default settings that are tailored for the
edge use case may need to be adjusted when using Envoy in a multi-level deployment as a
"level two" HTTP/2 proxy.

.. image:: /_static/multilevel_deployment.svg

**In summary, if you run level two Envoy version 1.11.1 or greater which terminates 
HTTP/2, we strongly advise you to change the HTTP/2 configuration of your level 
two Envoy, by setting its downstream**
:ref:`validation of HTTP/2 messaging option <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.stream_error_on_invalid_http_messaging>`
**to true.**

If there is an invalid HTTP/2 request and this option is not set, the Envoy in 
question will reset the entire connection. This behavior was changed as part of 
the 1.11.1 security release, to increase the security of Edge Envoys. Unfortunately, 
because there are no guarantees that edge proxies will enforce HTTP/1 or HTTP/2 
standards compliance as rigorously as Envoy’s HTTP/2 stack does, this can result 
in a problem as follows. If one client sends a request that for example passes 
level one proxy's validation checks, and it is forwarded over an upstream multiplexed 
HTTP/2 connection (potentially shared with other clients) the strict enforcement on 
the level two Envoy HTTP/2 will reset all the streams on that connection, causing 
a service disruption to the clients sharing that L1-L2 connection. If a malicious 
user has insight into what traffic will bypass level one checks, they could spray
“bad” traffic across the level one fleet, causing serious disruption to other users’ 
traffic.

Please note that the
:ref:`validation of HTTP/2 messaging option <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.stream_error_on_invalid_http_messaging>`
is planned to be deprecated and replaced with mandatory configuration in the HttpConnectionManager, to ensure
that what is now an easily overlooked option would need to be configured, ideally
appropriately for the given Envoy deployment. Please refer to the
https://github.com/envoyproxy/envoy/issues/9285 for more information.
