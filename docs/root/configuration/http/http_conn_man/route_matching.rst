.. _config_http_conn_man_route_table_route_matching:

Route matching
==============

When Envoy matches a route, it uses the following procedure:

#. The HTTP request's *host* or *:authority* header is matched to a :ref:`virtual host
   <envoy_v3_api_msg_config.route.v3.VirtualHost>`.
#. Each :ref:`route entry <envoy_v3_api_msg_config.route.v3.Route>` in the virtual host is checked,
   *in order*. If there is a match, the route is used and no further route checks are made.
#. Independently, each :ref:`virtual cluster <envoy_v3_api_msg_config.route.v3.VirtualCluster>` in the
   virtual host is checked, *in order*. If there is a match, the virtual cluster is used and no
   further virtual cluster checks are made.
