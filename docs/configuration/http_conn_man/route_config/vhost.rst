.. _config_http_conn_man_route_table_vhost:

Virtual host
============

The top level element in the routing configuration is a virtual host. Each virtual host has
a logical name as well as a set of domains that get routed to it based on the incoming request's
host header. This allows a single listener to service multiple top level domain path trees. Once a
virtual host is selected based on the domain, the routes are processed in order to see which
upstream cluster to route to or whether to perform a redirect.

.. code-block:: json

  {
    "name": "...",
    "domains": [],
    "routes": [],
    "require_ssl": "...",
    "virtual_clusters": [],
    "rate_limits": []
  }

name
  *(required, string)* The logical name of the virtual host. This is used when emitting certain
  statistics but is not relevant for forwarding.

domains
  *(required, array)* A list of domains (host/authority header) that will be matched to this
  virtual host. Currently, wildcard matching of the form "\*.foo.com" is not supported, however
  a special entry "\*" is allowed which will match any host/authority header. Only a single virtual
  host in the entire route configuration can match on "\*". A domain must be unique across all
  virtual hosts or the config will fail to load.

:ref:`routes <config_http_conn_man_route_table_route>`
  *(required, array)* The list of routes that will be matched, in order, for incoming requests.
  The first route that matches will be used.

.. _config_http_conn_man_route_table_vhost_require_ssl:

require_ssl
  *(optional, string)* Specifies the type of TLS enforcement the virtual host expects. Possible
  values are:

  all
    All requests must use TLS. If a request is not using TLS, a 302 redirect will be sent telling
    the client to use HTTPS.

  external_only
    External requests must use TLS. If a request is external and it is not using TLS, a 302 redirect
    will be sent telling the client to use HTTPS.

  If this option is not specified, there is no TLS requirement for the virtual host.

:ref:`virtual_clusters <config_http_conn_man_route_table_vcluster>`
  *(optional, array)* A list of virtual clusters defined for this virtual host. Virtual clusters
  are used for additional statistics gathering.

:ref:`rate_limits <config_http_conn_man_route_table_rate_limit_config>`
  *(optional, array)* Specifies a set of rate limit configurations that will be applied to the
  virtual host.
