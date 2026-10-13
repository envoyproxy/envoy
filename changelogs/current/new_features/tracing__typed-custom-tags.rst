Added a :ref:`value_type <envoy_v3_api_field_type.tracing.v3.CustomTag.value_type>` field to tracing
custom tags. When set to ``INT``, ``DOUBLE`` or ``BOOL``, tracers that support typed span attributes
(such as OpenTelemetry) emit the custom tag as a native integer, floating-point or boolean span
attribute instead of a string. Applies to every custom tag type. When unset (``UNSPECIFIED``) or set
to ``STRING`` the tag keeps the preexisting string behavior for all tracers.
