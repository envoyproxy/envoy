.. _config_network_filters_zookeeper_proxy:

ZooKeeper proxy
===============

The ZooKeeper proxy filter decodes the client protocol for
`Apache ZooKeeper <https://zookeeper.apache.org/>`_. It decodes the requests,
responses and events in the payload. Most opcodes known in
`ZooKeeper 3.5 <https://github.com/apache/zookeeper/blob/master/zookeeper-server/src/main/java/org/apache/zookeeper/ZooDefs.java>`_
are supported. The unsupported ones are related to SASL authentication.

:ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.zookeeper_proxy.v3.ZooKeeperProxy>`

.. attention::

   The zookeeper_proxy filter is experimental and is currently under active
   development. Capabilities will be expanded over time and the
   configuration structures are likely to change.

.. _config_network_filters_zookeeper_proxy_config:

Configuration
-------------

The ZooKeeper proxy filter should be chained with the TCP proxy filter as shown
in the configuration snippet below:

.. literalinclude:: _include/zookeeper-filter-proxy.yaml
    :language: yaml

.. _config_network_filters_zookeeper_proxy_stats:

Statistics
----------

Every configured ZooKeeper proxy filter has statistics rooted at *<stat_prefix>.zookeeper.*.

*_resp_fast* and *_resp_slow* are per-opcode metrics and will only be emitted when :ref:`enable_latency_threshold_metrics <envoy_v3_api_field_extensions.filters.network.zookeeper_proxy.v3.ZooKeeperProxy.enable_latency_threshold_metrics>` is set to ``true``.

*_rq_bytes* are per-opcode metrics and will only be emitted when :ref:`enable_per_opcode_request_bytes_metrics <envoy_v3_api_field_extensions.filters.network.zookeeper_proxy.v3.ZooKeeperProxy.enable_per_opcode_request_bytes_metrics>` is set to ``true``.

*_resp_bytes* are per-opcode metrics and will only be emitted when :ref:`enable_per_opcode_response_bytes_metrics <envoy_v3_api_field_extensions.filters.network.zookeeper_proxy.v3.ZooKeeperProxy.enable_per_opcode_response_bytes_metrics>` is set to ``true``.

*_decoder_error* are per-opcode metrics and will only be emitted when :ref:`enable_per_opcode_decoder_error_metrics <envoy_v3_api_field_extensions.filters.network.zookeeper_proxy.v3.ZooKeeperProxy.enable_per_opcode_decoder_error_metrics>` is set to ``true``.

The following counters are available:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  decoder_error, Counter, Number of times a message wasn't decoded
  connect_decoder_error, Counter, Number of times a connect request message wasn't decoded
  ping_decoder_error, Counter, Number of times a ping request message wasn't decoded
  auth_decoder_error, Counter, Number of times a auth request message wasn't decoded
  getdata_decoder_error, Counter, Number of times a getdata request message wasn't decoded
  create_decoder_error, Counter, Number of times a create request message wasn't decoded
  create2_decoder_error, Counter, Number of times a create2 request message wasn't decoded
  createcontainer_decoder_error, Counter, Number of times a createcontainer request message wasn't decoded
  createttl_decoder_error, Counter, Number of times a createttl request message wasn't decoded
  setdata_decoder_error, Counter, Number of times a setdata request message wasn't decoded
  getchildren_decoder_error, Counter, Number of times a getchildren request message wasn't decoded
  getchildren2_decoder_error, Counter, Number of times a getchildren2 request message wasn't decoded
  getephemerals_decoder_error, Counter, Number of times a getephemerals request message wasn't decoded
  getallchildrennumber_decoder_error, Counter, Number of times a getallchildrennumber request message wasn't decoded
  delete_decoder_error, Counter, Number of times a delete request message wasn't decoded
  exists_decoder_error, Counter, Number of times a exists request message wasn't decoded
  getacl_decoder_error, Counter, Number of times a getacl request message wasn't decoded
  setacl_decoder_error, Counter, Number of times a setacl request message wasn't decoded
  sync_decoder_error, Counter, Number of times a sync request message wasn't decoded
  multi_decoder_error, Counter, Number of times a multi request message wasn't decoded
  reconfig_decoder_error, Counter, Number of times a reconfig request message wasn't decoded
  close_decoder_error, Counter, Number of times a close request message wasn't decoded
  setauth_decoder_error, Counter, Number of times a setauth request message wasn't decoded
  setwatches_decoder_error, Counter, Number of times a setwatches request message wasn't decoded
  setwatches2_decoder_error, Counter, Number of times a setwatches2 request message wasn't decoded
  addwatch_decoder_error, Counter, Number of times a addwatch request message wasn't decoded
  checkwatches_decoder_error, Counter, Number of times a checkwatches request message wasn't decoded
  removewatches_decoder_error, Counter, Number of times a removewatches request message wasn't decoded
  check_decoder_error, Counter, Number of times a check request message wasn't decoded
  request_bytes, Counter, Number of bytes in decoded request messages
  connect_rq_bytes, Counter, Number of bytes in decoded connect request messages
  ping_rq_bytes, Counter, Number of bytes in decoded ping request messages
  auth_rq_bytes, Counter, Number of bytes in decoded auth request messages
  getdata_rq_bytes, Counter, Number of bytes in decoded getdata request messages
  create_rq_bytes, Counter, Number of bytes in decoded create request messages
  create2_rq_bytes, Counter, Number of bytes in decoded create2 request messages
  createcontainer_rq_bytes, Counter, Number of bytes in decoded createcontainer request messages
  createttl_rq_bytes, Counter, Number of bytes in decoded createttl request messages
  setdata_rq_bytes, Counter, Number of bytes in decoded setdata request messages
  getchildren_rq_bytes, Counter, Number of bytes in decoded getchildren request messages
  getchildren2_rq_bytes, Counter, Number of bytes in decoded getchildren2 request messages
  delete_rq_bytes, Counter, Number of bytes in decoded delete request messages
  exists_rq_bytes, Counter, Number of bytes in decoded exists request messages
  getacl_rq_bytes, Counter, Number of bytes in decoded getacl request messages
  setacl_rq_bytes, Counter, Number of bytes in decoded setacl request messages
  sync_rq_bytes, Counter, Number of bytes in decoded sync request messages
  check_rq_bytes, Counter, Number of bytes in decoded check request messages
  multi_rq_bytes, Counter, Number of bytes in decoded multi request messages
  reconfig_rq_bytes, Counter, Number of bytes in decoded reconfig request messages
  setwatches_rq_bytes, Counter, Number of bytes in decoded setwatches request messages
  setwatches2_rq_bytes, Counter, Number of bytes in decoded setwatches2 request messages
  addwatch_rq_bytes, Counter, Number of bytes in decoded addwatch request messages
  checkwatches_rq_bytes, Counter, Number of bytes in decoded checkwatches request messages
  removewatches_rq_bytes, Counter, Number of bytes in decoded removewatches request messages
  getephemerals_rq_bytes, Counter, Number of bytes in decoded getephemerals request messages
  getallchildrennumber_rq_bytes, Counter, Number of bytes in decoded getallchildrennumber request messages
  close_rq_bytes, Counter, Number of bytes in decoded close request messages
  connect_rq, Counter, Number of regular connect (non-readonly) requests
  connect_readonly_rq, Counter, Number of connect requests with the readonly flag set
  ping_rq, Counter, Number of ping requests
  auth.<type>_rq, Counter, Number of auth requests for a given type
  getdata_rq, Counter, Number of getdata requests
  create_rq, Counter, Number of create requests
  create2_rq, Counter, Number of create2 requests
  createcontainer_rq, Counter, Number of createcontainer requests
  createttl_rq, Counter, Number of createttl requests
  setdata_rq, Counter, Number of setdata requests
  getchildren_rq, Counter, Number of getchildren requests
  getchildren2_rq, Counter, Number of getchildren2 requests
  delete_rq, Counter, Number of delete requests
  exists_rq, Counter, Number of stat requests
  getacl_rq, Counter, Number of getacl requests
  setacl_rq, Counter, Number of setacl requests
  sync_rq, Counter, Number of sync requests
  check_rq, Counter, Number of check requests
  multi_rq, Counter, Number of multi transaction requests
  reconfig_rq, Counter, Number of reconfig requests
  setwatches_rq, Counter, Number of setwatches requests
  setwatches2_rq, Counter, Number of setwatches2 requests
  addwatch_rq, Counter, Number of addwatch requests
  checkwatches_rq, Counter, Number of checkwatches requests
  removewatches_rq, Counter, Number of removewatches requests
  getephemerals_rq, Counter, Number of getephemerals requests
  getallchildrennumber_rq, Counter, Number of getallchildrennumber requests
  close_rq, Counter, Number of close requests
  response_bytes, Counter, Number of bytes in decoded response messages
  connect_resp_bytes, Counter, Number of bytes in decoded connect response messages
  ping_resp_bytes, Counter, Number of bytes in decoded ping response messages
  auth_resp_bytes, Counter, Number of bytes in decoded auth response messages
  getdata_resp_bytes, Counter, Number of bytes in decoded getdata response messages
  create_resp_bytes, Counter, Number of bytes in decoded create response messages
  create2_resp_bytes, Counter, Number of bytes in decoded create2 response messages
  createcontainer_resp_bytes, Counter, Number of bytes in decoded createcontainer response messages
  createttl_resp_bytes, Counter, Number of bytes in decoded createttl response messages
  setdata_resp_bytes, Counter, Number of bytes in decoded setdata response messages
  getchildren_resp_bytes, Counter, Number of bytes in decoded getchildren response messages
  getchildren2_resp_bytes, Counter, Number of bytes in decoded getchildren2 response messages
  delete_resp_bytes, Counter, Number of bytes in decoded delete response messages
  exists_resp_bytes, Counter, Number of bytes in decoded exists response messages
  getacl_resp_bytes, Counter, Number of bytes in decoded getacl response messages
  setacl_resp_bytes, Counter, Number of bytes in decoded setacl response messages
  sync_resp_bytes, Counter, Number of bytes in decoded sync response messages
  check_resp_bytes, Counter, Number of bytes in decoded check response messages
  multi_resp_bytes, Counter, Number of bytes in decoded multi response messages
  reconfig_resp_bytes, Counter, Number of bytes in decoded reconfig response messages
  setwatches_resp_bytes, Counter, Number of bytes in decoded setwatches response messages
  setwatches2_resp_bytes, Counter, Number of bytes in decoded setwatches2 response messages
  addwatch_resp_bytes, Counter, Number of bytes in decoded addwatch response messages
  checkwatches_resp_bytes, Counter, Number of bytes in decoded checkwatches response messages
  removewatches_resp_bytes, Counter, Number of bytes in decoded removewatches response messages
  getephemerals_resp_bytes, Counter, Number of bytes in decoded getephemerals response messages
  getallchildrennumber_resp_bytes, Counter, Number of bytes in decoded getallchildrennumber response messages
  close_resp_bytes, Counter, Number of bytes in decoded close response messages
  connect_resp, Counter, Number of connect responses
  ping_resp, Counter, Number of ping responses
  auth_resp, Counter, Number of auth responses
  getdata_resp, Counter, Number of getdata responses
  create_resp, Counter, Number of create responses
  create2_resp, Counter, Number of create2 responses
  createcontainer_resp, Counter, Number of createcontainer responses
  createttl_resp, Counter, Number of createttl responses
  setdata_resp, Counter, Number of setdata responses
  getchildren_resp, Counter, Number of getchildren responses
  getchildren2_resp, Counter, Number of getchildren2 responses
  delete_resp, Counter, Number of delete responses
  exists_resp, Counter, Number of exists responses
  getacl_resp, Counter, Number of getacl responses
  setacl_resp, Counter, Number of setacl responses
  sync_resp, Counter, Number of sync responses
  check_resp, Counter, Number of check responses
  multi_resp, Counter, Number of multi responses
  reconfig_resp, Counter, Number of reconfig responses
  setauth_resp, Counter, Number of setauth responses
  setwatches_resp, Counter, Number of setwatches responses
  setwatches2_resp, Counter, Number of setwatches2 responses
  addwatch_resp, Counter, Number of addwatch responses
  checkwatches_resp, Counter, Number of checkwatches responses
  removewatches_resp, Counter, Number of removewatches responses
  getephemerals_resp, Counter, Number of getephemerals responses
  getallchildrennumber_resp, Counter, Number of getallchildrennumber responses
  close_resp, Counter, Number of close responses
  watch_event, Counter, Number of watch events fired by the server
  connect_resp_fast, Counter, Number of connect responses faster than or equal to the threshold
  ping_resp_fast, Counter, Number of ping responses faster than or equal to the threshold
  auth_resp_fast, Counter, Number of auth responses faster than or equal to the threshold
  getdata_resp_fast, Counter, Number of getdata responses faster than or equal to the threshold
  create_resp_fast, Counter, Number of create responses faster than or equal to the threshold
  create2_resp_fast, Counter, Number of create2 responses faster than or equal to the threshold
  createcontainer_resp_fast, Counter, Number of createcontainer responses faster than or equal to the threshold
  createttl_resp_fast, Counter, Number of createttl responses faster than or equal to the threshold
  setdata_resp_fast, Counter, Number of setdata responses faster than or equal to the threshold
  getchildren_resp_fast, Counter, Number of getchildren responses faster than or equal to the threshold
  getchildren2_resp_fast, Counter, Number of getchildren2 responses faster than or equal to the threshold
  delete_resp_fast, Counter, Number of delete responses faster than or equal to the threshold
  exists_resp_fast, Counter, Number of exists responses faster than or equal to the threshold
  getacl_resp_fast, Counter, Number of getacl responses faster than or equal to the threshold
  setacl_resp_fast, Counter, Number of setacl responses faster than or equal to the threshold
  sync_resp_fast, Counter, Number of sync responses faster than or equal to the threshold
  check_resp_fast, Counter, Number of check responses faster than or equal to the threshold
  multi_resp_fast, Counter, Number of multi responses faster than or equal to the threshold
  reconfig_resp_fast, Counter, Number of reconfig responses faster than or equal to the threshold
  setauth_resp_fast, Counter, Number of setauth responses faster than or equal to the threshold
  setwatches_resp_fast, Counter, Number of setwatches responses faster than or equal to the threshold
  setwatches2_resp_fast, Counter, Number of setwatches2 responses faster than or equal to the threshold
  addwatch_resp_fast, Counter, Number of addwatch responses faster than or equal to the threshold
  checkwatches_resp_fast, Counter, Number of checkwatches responses faster than or equal to the threshold
  removewatches_resp_fast, Counter, Number of removewatches responses faster than or equal to the threshold
  getephemerals_resp_fast, Counter, Number of getephemerals responses faster than or equal to the threshold
  getallchildrennumber_resp_fast, Counter, Number of getallchildrennumber responses faster than or equal to the threshold
  close_resp_fast, Counter, Number of close responses faster than or equal to the threshold
  connect_resp_slow, Counter, Number of connect responses slower than the threshold
  ping_resp_slow, Counter, Number of ping responses slower than the threshold
  auth_resp_slow, Counter, Number of auth responses slower than the threshold
  getdata_resp_slow, Counter, Number of getdata responses slower than the threshold
  create_resp_slow, Counter, Number of create responses slower than the threshold
  create2_resp_slow, Counter, Number of create2 responses slower than the threshold
  createcontainer_resp_slow, Counter, Number of createcontainer responses slower than the threshold
  createttl_resp_slow, Counter, Number of createttl responses slower than the threshold
  setdata_resp_slow, Counter, Number of setdata responses slower than the threshold
  getchildren_resp_slow, Counter, Number of getchildren responses slower than the threshold
  getchildren2_resp_slow, Counter, Number of getchildren2 responses slower than the threshold
  delete_resp_slow, Counter, Number of delete responses slower than the threshold
  exists_resp_slow, Counter, Number of exists responses slower than the threshold
  getacl_resp_slow, Counter, Number of getacl responses slower than the threshold
  setacl_resp_slow, Counter, Number of setacl responses slower than the threshold
  sync_resp_slow, Counter, Number of sync responses slower than the threshold
  check_resp_slow, Counter, Number of check responses slower than the threshold
  multi_resp_slow, Counter, Number of multi responses slower than the threshold
  reconfig_resp_slow, Counter, Number of reconfig responses slower than the threshold
  setauth_resp_slow, Counter, Number of setauth responses slower than the threshold
  setwatches_resp_slow, Counter, Number of setwatches responses slower than the threshold
  setwatches2_resp_slow, Counter, Number of setwatches2 responses slower than the threshold
  addwatch_resp_slow, Counter, Number of addwatch responses slower than the threshold
  checkwatches_resp_slow, Counter, Number of checkwatches responses slower than the threshold
  removewatches_resp_slow, Counter, Number of removewatches responses slower than the threshold
  getephemerals_resp_slow, Counter, Number of getephemerals responses slower than the threshold
  getallchildrennumber_resp_slow, Counter, Number of getallchildrennumber responses slower than the threshold
  close_resp_slow, Counter, Number of close responses slower than the threshold


.. _config_network_filters_zookeeper_proxy_latency_stats:

Per opcode latency statistics
-----------------------------

The filter will gather latency statistics in the *<stat_prefix>.zookeeper.<opcode>_resp_latency* namespace.
Latency stats are in milliseconds:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  connect_response_latency, Histogram, Opcode execution time in milliseconds
  ping_response_latency, Histogram, Opcode execution time in milliseconds
  auth_response_latency, Histogram, Opcode execution time in milliseconds
  watch_event, Histogram, Opcode execution time in milliseconds
  getdata_resp_latency, Histogram, Opcode execution time in milliseconds
  create_resp_latency, Histogram, Opcode execution time in milliseconds
  create2_resp_latency, Histogram, Opcode execution time in milliseconds
  createcontainer_resp_latency, Histogram, Opcode execution time in milliseconds
  createttl_resp_latency, Histogram, Opcode execution time in milliseconds
  setdata_resp_latency, Histogram, Opcode execution time in milliseconds
  getchildren_resp_latency, Histogram, Opcode execution time in milliseconds
  getchildren2_resp_latency, Histogram, Opcode execution time in milliseconds
  getephemerals_resp_latency, Histogram, Opcode execution time in milliseconds
  getallchildrennumber_resp_latency, Histogram, Opcode execution time in milliseconds
  delete_resp_latency, Histogram, Opcode execution time in milliseconds
  exists_resp_latency, Histogram, Opcode execution time in milliseconds
  getacl_resp_latency, Histogram, Opcode execution time in milliseconds
  setacl_resp_latency, Histogram, Opcode execution time in milliseconds
  sync_resp_latency, Histogram, Opcode execution time in milliseconds
  multi_resp_latency, Histogram, Opcode execution time in milliseconds
  reconfig_resp_latency, Histogram, Opcode execution time in milliseconds
  close_resp_latency, Histogram, Opcode execution time in milliseconds
  setauth_resp_latency, Histogram, Opcode execution time in milliseconds
  setwatches_resp_latency, Histogram, Opcode execution time in milliseconds
  setwatches2_resp_latency, Histogram, Opcode execution time in milliseconds
  addwatch_resp_latency, Histogram, Opcode execution time in milliseconds
  checkwatches_resp_latency, Histogram, Opcode execution time in milliseconds
  removewatches_resp_latency, Histogram, Opcode execution time in milliseconds
  check_resp_latency, Histogram, Opcode execution time in milliseconds


.. _config_network_filters_zookeeper_proxy_dynamic_metadata:

Dynamic Metadata
----------------

The ZooKeeper filter emits the following dynamic metadata for each message parsed:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  <path>, string, "The path associated with the request, response or event"
  <opname>, string, "The opname for the request, response or event"
  <create_type>, string, "The string representation of the flags applied to the znode"
  <bytes>, string, "The size of the request message in bytes"
  <watch>, string, "True if a watch is being set, false otherwise"
  <version>, string, "The version parameter, if any, given with the request"
  <timeout>, string, "The timeout parameter in a connect response"
  <protocol_version>, string, "The protocol version in a connect response"
  <readonly>, string, "The readonly flag in a connect response"
  <zxid>, string, "The zxid field in a response header"
  <error>, string, "The error field in a response header"
  <client_state>, string, "The state field in a watch event"
  <event_type>, string, "The event type in a a watch event"
