Fixed a bug where :ref:`omit_empty_values
<envoy_v3_api_field_config.core.v3.SubstitutionFormatString.omit_empty_values>` had no effect for
``json_format``. Because the JSON formatter pre-serializes the template when loading the
configuration, keys whose command operators evaluated to null were still emitted (for example
``{"key":null}`` instead of ``{}``). When ``omit_empty_values`` is set, the JSON formatter now
omits keys with null values, removes nested objects that become empty, and preserves empty arrays,
matching the documented behavior. This behavioral change can be reverted by setting the runtime
guard ``envoy.reloadable_features.json_formatter_omit_empty_values`` to ``false``.
