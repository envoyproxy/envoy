Fixed a bug where :ref:`response_headers_to_remove
<envoy_v3_api_field_config.route.v3.Route.response_headers_to_remove>` was not applied to proxied
upstream ``1xx`` informational responses (such as ``100 Continue`` and ``103 Early Hints``) when
:ref:`proxy_100_continue
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.proxy_100_continue>`
was enabled. This behavioral change can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.response_headers_to_remove_on_1xx`` to ``false``.
