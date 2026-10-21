Fixed a performance issue where metadata command operators such as ``%DYNAMIC_METADATA(...)%`` copied the
whole metadata each time they produced a typed value, for example for the fields of a
:ref:`json_format <envoy_v3_api_field_config.core.v3.SubstitutionFormatString.json_format>` access log.
