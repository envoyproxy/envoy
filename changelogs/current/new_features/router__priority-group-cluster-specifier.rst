Added a new :ref:`priority group cluster specifier
<envoy_v3_api_msg_extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupClusterSpecifier>`
that splits the candidate clusters of a route into a list of named groups. The group is selected
based on the attempt count of the request (the initial attempt uses the first group, the first retry
uses the second group, and so on, wrapping around when the attempt count exceeds the number of the
groups) and the target cluster is then selected from the group based on the cluster weights. The
groups can be overridden per request by an optional dynamic metadata entry. The target cluster
is refreshed for every attempt, so this can be combined with :ref:`refresh_cluster_on_retry
<envoy_v3_api_field_config.route.v3.RetryPolicy.refresh_cluster_on_retry>` to retry a request in a
different priority group.
