Fixed a bug where redirect routes using :ref:`UriTemplateMatchConfig
<envoy_v3_api_msg_extensions.path.match.uri_template.v3.UriTemplateMatchConfig>` with
:ref:`prefix_rewrite <envoy_v3_api_field_config.route.v3.RedirectAction.prefix_rewrite>` replaced
the literal URI template pattern instead of the matched request path. This behavioral change can be
temporarily reverted by setting the runtime guard
``envoy.reloadable_features.uri_template_redirect_use_request_path`` to ``false``.
