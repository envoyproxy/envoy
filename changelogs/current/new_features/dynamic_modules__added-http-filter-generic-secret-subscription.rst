Added generic secret subscriptions to the dynamic modules HTTP filter. During filter config
initialization a module can subscribe to a generic secret by name, optionally passing a JSON
serialized :ref:`ConfigSource <envoy_v3_api_msg_config.core.v3.ConfigSource>` to fetch it over SDS,
and read the current value per-stream or from the config context afterwards. Values are kept
up-to-date as the SDS server pushes new versions. Available through the Rust, Go and C++ SDKs as
``subscribe_generic_secret``/``get_generic_secret``, ``SubscribeGenericSecret``/``GetGenericSecret``
and ``subscribeGenericSecret``/``getGenericSecret`` respectively.
