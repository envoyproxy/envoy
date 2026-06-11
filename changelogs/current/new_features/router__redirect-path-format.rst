Added :ref:`path_rewrite <envoy_v3_api_field_config.route.v3.RedirectAction.path_rewrite>`
to ``RedirectAction``, a redirect action that constructs the redirect path using
:ref:`substitution format specifiers <config_access_log_format>` and CEL expressions,
enabling dynamic paths based on request headers, downstream connection attributes, and other
stream metadata. Supported in route-level redirects and in the custom response
:ref:`redirect policy <envoy_v3_api_msg_extensions.http.custom_response.redirect_policy.v3.RedirectPolicy>`.
