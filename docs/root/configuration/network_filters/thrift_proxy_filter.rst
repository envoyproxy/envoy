.. _config_network_filters_thrift_proxy:

Thrift proxy
============

* :ref:`v2 API reference <envoy_api_msg_config.filter.network.thrift_proxy.v2alpha1.ThriftProxy>`

Cluster Protocol Options
------------------------

Thrift connections to upstream hosts can be configured by adding an entry to the appropriate
Cluster's :ref:`extension_protocol_options<envoy_api_field_Cluster.extension_protocol_options>`
keyed by `envoy.filters.network.thrift_proxy`. The
:ref:`ThriftProtocolOptions<envoy_api_msg_config.filter.network.thrift_proxy.v2alpha1.ThriftProtocolOptions>`
message describes the available options.

Thrift Request Metadata
-----------------------

The :ref:`HEADER transport<envoy_api_enum_value_config.filter.network.thrift_proxy.v2alpha1.TransportType.HEADER>`
and :ref:`TWITTER protocol<envoy_api_enum_value_config.filter.network.thrift_proxy.v2alpha1.ProtocolType.TWITTER>`
support metadata. In particular, the
`Header transport <https://github.com/apache/thrift/blob/master/doc/specs/HeaderFormat.md>`_
supports informational key/value pairs and the Twitter protocol transmits
`tracing and request context data <https://github.com/twitter/finagle/blob/master/finagle-thrift/src/main/thrift/tracing.thrift>`_.

Header Transport Metadata
~~~~~~~~~~~~~~~~~~~~~~~~~

Header transport key/value pairs are available for routing as
:ref:`headers <envoy_api_field_config.filter.network.thrift_proxy.v2alpha1.RouteMatch.headers>`.

Twitter Protocol Metadata
~~~~~~~~~~~~~~~~~~~~~~~~~

Twitter protocol request contexts are converted into headers which are available for routing as
:ref:`headers <envoy_api_field_config.filter.network.thrift_proxy.v2alpha1.RouteMatch.headers>`.
In addition, the following fields are presented as headers:

Client Identifier
    The ClientId's `name` field (nested in the RequestHeader `client_id` field) becomes the
    `:client-id` header.

Destination
    The RequestHeader `dest` field becomes the `:dest` header.

Delegations
    Each Delegation from the RequestHeader `delegations` field is added as a header. The header
    name is the prefix `:d:` followed by the Delegation's `src`. The value is the Delegation's
    `dst` field.

Metadata Interoperability
~~~~~~~~~~~~~~~~~~~~~~~~~

Request metadata that is available for routing (see above) is automatically converted between wire
formats when translation between downstream and upstream connections occurs. Twitter protocol
request contexts, client id, destination, and delegations are therefore presented as Header
transport key/value pairs, named as above. Similarly, Header transport key/value pairs are
presented as Twitter protocol RequestContext values, unless they match the special names described
above. For instance, a downstream Header transport request with the info key ":client-id" is
translated to an upstream Twitter protocol request with a ClientId value.
