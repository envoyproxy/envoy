.. _envoy_v3_api_file_contrib.envoy.extensions.filters.listener.postgres_inspector.v3alpha.postgres_inspector.proto:

Postgres Inspector Listener Filter v3alpha
=========================================

.. toctree::
  :maxdepth: 2

.. _envoy_v3_api_msg_contrib.envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector:

contrib.envoy.extensions.filters.listener.postgres_inspector.v3alpha.PostgresInspector
-----------------------------------------------------------------------------------
:repo:`contrib/envoy/extensions/filters/listener/postgres_inspector/v3alpha/postgres_inspector.proto` {#contrib/envoy/extensions/filters/listener/postgres_inspector/v3alpha/postgres_inspector.proto}

.. code-block:: proto3

  message PostgresInspector {
    google.protobuf.BoolValue enable_metadata_extraction = 1;
    // PostgreSQL defines MAX_STARTUP_PACKET_LENGTH as 10KB. The valid range is in between 256 and 10000 bytes.
    google.protobuf.UInt32Value max_startup_message_size = 2 [(validate.rules).uint32 = {lte: 10000 gte: 256}];
    google.protobuf.Duration startup_timeout = 3 [(validate.rules).duration = {gte {seconds: 1}}];
  }

.. _envoy_v3_api_msg_contrib.envoy.extensions.filters.listener.postgres_inspector.v3alpha.StartupMetadata:

contrib.envoy.extensions.filters.listener.postgres_inspector.v3alpha.StartupMetadata
---------------------------------------------------------------------------------
:repo:`contrib/envoy/extensions/filters/listener/postgres_inspector/v3alpha/postgres_inspector.proto` {#contrib/envoy/extensions/filters/listener/postgres_inspector/v3alpha/postgres_inspector.proto}

.. code-block:: proto3

  message StartupMetadata {
    string user = 1;
    string database = 2;
    string application_name = 3;
  }
