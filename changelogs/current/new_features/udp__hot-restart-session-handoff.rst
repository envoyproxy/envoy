Added UDP session handoff during hot restart: the parent instance keeps serving established
UDP sessions while draining and forwards datagrams of unknown sessions to the child instance over
the hot restart RPC. Connectionless UDP listeners no longer stop functioning between
drain start and parent shutdown. The set of served sessions is the set a listener filter has
registered. Currently only :ref:`udp_proxy <config_udp_listener_filters_udp_proxy>` registers
its sticky sessions, which the parent keeps serving until they reach the
:ref:`idle timeout <envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.idle_timeout>`.
