Fixed a bug where commands configured via :ref:`custom_commands
<envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.custom_commands>` were rejected
within ``MULTI``/``EXEC`` transactions. Custom commands are now treated as simple commands within
transactions, matching their behavior outside of transactions.
