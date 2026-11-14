The ``Node`` message now reports the names of configured bootstrap extensions in
``configured_bootstrap_extensions``. This lets xDS management servers distinguish extensions
configured at startup from extensions that are only compiled into the Envoy binary.
