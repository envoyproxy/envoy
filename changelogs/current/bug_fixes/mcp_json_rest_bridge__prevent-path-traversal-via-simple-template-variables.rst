Fixed a path-traversal issue in the ``mcp_json_rest_bridge`` HTTP filter where a path-template
variable's value (taken from attacker-controlled tool-call arguments) was installed verbatim into
the upstream request ``:path``, so a value such as ``../../admin/secrets`` produced raw path
traversal. Traversal segments (``.`` / ``..``) are now rejected for every template variable, and a
"simple" variable (for example ``{id}``) additionally has ``/`` percent-encoded to confine it to a
single path segment. Variables with an explicit pattern such as ``{name=projects/*}`` may still
legitimately span multiple segments.
