The :ref:`LocalResponsePolicy <envoy_v3_api_msg_extensions.http.custom_response.local_response_policy.v3.LocalResponsePolicy>`
can now optionally preserve the existing ``response_code_details`` or set an explicit
value via ``preserve_response_code_details`` / ``response_code_details``. Unset continues
to clear details (legacy behavior).
