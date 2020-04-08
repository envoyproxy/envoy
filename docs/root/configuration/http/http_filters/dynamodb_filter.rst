.. _config_http_filters_dynamo:

DynamoDB
========

* DynamoDB :ref:`architecture overview <arch_overview_dynamo>`
* :ref:`v3 API reference <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.name>`
* This filter should be configured with the name *envoy.filters.http.dynamo*.

Statistics
----------

The DynamoDB filter outputs statistics in the *http.<stat_prefix>.dynamodb.* namespace. The :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>` comes from the
owning HTTP connection manager.

Per operation stats can be found in the *http.<stat_prefix>.dynamodb.operation.<operation_name>.*
namespace.

  .. csv-table::
    :header: Name, Type, Description
    :widths: 1, 1, 2

    upstream_rq_total, Counter, Total number of requests with <operation_name>
    upstream_rq_time, Histogram, Time spent on <operation_name>
    upstream_rq_total_xxx, Counter, Total number of requests with <operation_name> per response code (503/2xx/etc)
    upstream_rq_time_xxx, Histogram, Time spent on <operation_name> per response code (400/3xx/etc)

Per table stats can be found in the *http.<stat_prefix>.dynamodb.table.<table_name>.* namespace.
Most of the operations to DynamoDB involve a single table, but BatchGetItem and BatchWriteItem can
include several tables, Envoy tracks per table stats in this case only if it is the same table used
in all operations from the batch.

  .. csv-table::
    :header: Name, Type, Description
    :widths: 1, 1, 2

    upstream_rq_total, Counter, Total number of requests on <table_name> table
    upstream_rq_time, Histogram, Time spent on <table_name> table
    upstream_rq_total_xxx, Counter, Total number of requests on <table_name> table per response code (503/2xx/etc)
    upstream_rq_time_xxx, Histogram, Time spent on <table_name> table per response code (400/3xx/etc)

*Disclaimer: Please note that this is a pre-release Amazon DynamoDB feature that is not yet widely available.*
Per partition and operation stats can be found in the *http.<stat_prefix>.dynamodb.table.<table_name>.*
namespace. For batch operations, Envoy tracks per partition and operation stats only if it is the same
table used in all operations.

  .. csv-table::
    :header: Name, Type, Description
    :widths: 1, 1, 2

    capacity.<operation_name>.__partition_id=<last_seven_characters_from_partition_id>, Counter, Total number of capacity for <operation_name> on <table_name> table for a given <partition_id>

Additional detailed stats:

* For 4xx responses and partial batch operation failures, the total number of failures for a given
  table and failure are tracked in the *http.<stat_prefix>.dynamodb.error.<table_name>.* namespace.

  .. csv-table::
    :header: Name, Type, Description
    :widths: 1, 1, 2

    <error_type>, Counter, Total number of specific <error_type> for a given <table_name>
    BatchFailureUnprocessedKeys, Counter, Total number of partial batch failures for a given <table_name>

Runtime
-------

The DynamoDB filter supports the following runtime settings:

dynamodb.filter_enabled
  The % of requests for which the filter is enabled. Default is 100%.
