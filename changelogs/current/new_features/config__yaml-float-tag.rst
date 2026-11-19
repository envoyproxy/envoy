YAML configuration now honors the explicit ``!!float`` tag: a scalar such as ``!!float 5000.0`` is
loaded as a JSON number instead of a string, so it stays a number inside a
``google.protobuf.Struct`` (e.g. a ``typed_config`` packed as a ``Struct``). Untagged float literals
are unchanged and still load as strings. Non-finite values are loaded as the strings ``Infinity``,
``-Infinity`` and ``NaN``. A ``!!float`` scalar that cannot be decoded as a double now fails
configuration loading. See :ref:`special YAML usage <config_overview_special_yaml>`.
