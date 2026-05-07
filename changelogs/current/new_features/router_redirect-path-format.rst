Added :ref:`path_redirect_format <envoy_v3_api_field_config.route.v3.RedirectAction.path_redirect_format>`
to ``RedirectAction``, which is analogous to ``path_redirect`` but supports
:ref:`substitution format specifiers <config_access_log_format>` and CEL expressions.
This is supported both in route-level redirects and in the custom response
:ref:`redirect policy <envoy_v3_api_msg_extensions.http.custom_response.redirect_policy.v3.RedirectPolicy>`.