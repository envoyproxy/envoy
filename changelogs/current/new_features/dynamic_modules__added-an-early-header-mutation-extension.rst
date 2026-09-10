Added a dynamic modules early header mutation extension
(``envoy.http.early_header_mutation.dynamic_modules``) that lets a dynamic module rewrite request
headers before routing, tracing, request ID generation and any HTTP filter runs.
