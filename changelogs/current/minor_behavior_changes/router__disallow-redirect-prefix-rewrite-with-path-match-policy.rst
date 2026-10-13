Added validation to reject route configurations combining :ref:`prefix_rewrite
<envoy_v3_api_field_config.route.v3.RedirectAction.prefix_rewrite>` with
:ref:`path_match_policy <envoy_v3_api_field_config.route.v3.RouteMatch.path_match_policy>` in
redirect routes at configuration ingestion time. This behavioral change can be temporarily reverted
by setting runtime guard
``envoy.reloadable_features.disallow_redirect_prefix_rewrite_with_path_match_policy`` to ``false``.
