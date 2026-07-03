Added support for mixed variable/literal path segments in URI template matching and rewriting,
enabling patterns such as ``/api/v{version}/users/{id}.json`` where a variable is embedded within
a path segment alongside literal text. The surrounding prefix and suffix must match literally while
only the variable portion is captured. This is controlled by runtime guard
``envoy.reloadable_features.uri_template_mixed_variable_literals``, which is enabled by default.
