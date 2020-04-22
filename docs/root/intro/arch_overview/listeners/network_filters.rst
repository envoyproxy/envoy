.. _arch_overview_network_filters:

Network (L3/L4) filters
=======================

As discussed in the :ref:`listener <arch_overview_listeners>` section, network level (L3/L4) filters
form the core of Envoy connection handling. The filter API allows for different sets of filters to
be mixed and matched and attached to a given listener. There are three different types of network
filters:

* **Read**: Read filters are invoked when Envoy receives data from a downstream connection.
* **Write**: Write filters are invoked when Envoy is about to send data to a downstream connection.
* **Read/Write**: Read/Write filters are invoked both when Envoy receives data from a downstream
  connection and when it is about to send data to a downstream connection.

The API for network level filters is relatively simple since ultimately the filters operate on raw
bytes and a small number of connection events (e.g., TLS handshake complete, connection disconnected
locally or remotely, etc.). Filters in the chain can stop and subsequently continue iteration to
further filters. This allows for more complex scenarios such as calling a :ref:`rate limiting
service <arch_overview_global_rate_limit>`, etc. Network level filters can also share state (static and
dynamic) among themselves within the context of a single downstream connection. Refer to
:ref:`data sharing between filters <arch_overview_data_sharing_between_filters>` for more details.
Envoy already includes several network level filters that are documented in this architecture
overview as well as the :ref:`configuration reference <config_network_filters>`.
