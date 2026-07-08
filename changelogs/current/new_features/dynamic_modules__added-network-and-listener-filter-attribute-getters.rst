Added ``get_attribute_string``, ``get_attribute_int`` and ``get_attribute_bool`` to dynamic
module network and listener filters, backed by new ABI callbacks and exposed by the Rust SDK
on ``EnvoyNetworkFilter`` and ``EnvoyListenerFilter``. The shared attribute accessor now also
serves the ``response.flags`` and ``response.size`` integer attributes.
