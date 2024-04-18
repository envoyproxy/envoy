.. _arch_overview_dynamo:

DynamoDB
========

Envoy supports an HTTP level DynamoDB sniffing filter with the following features:

* DynamoDB API request/response parser.
* DynamoDB per operation/per table/per partition and operation statistics.
* Failure type statistics for 4xx responses, parsed from response JSON,
  e.g., ProvisionedThroughputExceededException.
* Batch operation partial failure statistics.

The DynamoDB filter is a good example of Envoy’s extensibility and core abstractions at the HTTP
layer, and can be used to filter all application communication with DynamoDB. It provides an
invaluable source of data agnostic to the application platform and specific AWS SDK in use.

DynamoDB filter :ref:`configuration <config_http_filters_dynamo>`.
