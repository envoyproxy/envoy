Added a :ref:`weighted priority cluster specifier
<envoy_v3_api_msg_extensions.router.cluster_specifiers.weighted_priority.v3.WeightedPriorityClusterSpecifier>`,
which selects a cluster by walking ordered priority groups and making a weighted choice among the
clusters of the first group that has a healthy cluster. Unlike :ref:`weighted_clusters
<envoy_v3_api_field_config.route.v3.RouteAction.weighted_clusters>`, a group whose clusters all lack
healthy hosts is skipped rather than absorbing its share of the traffic, and weights are
renormalized over whichever clusters remain healthy.
