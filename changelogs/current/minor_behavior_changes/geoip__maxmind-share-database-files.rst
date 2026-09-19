The MaxMind geolocation provider now loads each set of database files once and shares it between
every provider configured with the same set of files. As part of this, the database file level
statistics ``<db_type>.db_reload_success``, ``<db_type>.db_reload_error`` and
``<db_type>.db_build_epoch`` are now emitted in the server wide ``maxmind.`` namespace rather than
under the listener derived ``<stat_prefix>.maxmind.`` namespace.
