Fixed the JWT filter rewriting the whole query string when ``forward: false`` removes a token
carried in a query parameter. Envoy decoded every other parameter, sorted them and appended ``=``
to parameters without a value. Now only the token parameter is removed and the rest of the query
string is left as sent. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.jwt_authn_remove_query_param_in_place`` to ``false``.
