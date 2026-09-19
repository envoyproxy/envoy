The MaxMind geolocation provider now loads each geolocation database file once and shares it
between every provider configured with that file, rather than loading a separate copy per
provider. As part of this, the database file level statistics ``db_reload_success``,
``db_reload_error`` and ``db_build_epoch`` are no longer emitted under the listener derived
``<stat_prefix>.maxmind.`` namespace. They are now emitted as
``maxmind.<db_type>.<db_name>.<statistic>``, where ``db_name`` is the path of the database file and
is also available as a ``db_name`` tag, and they are removed once no listener uses the provider any
more.
