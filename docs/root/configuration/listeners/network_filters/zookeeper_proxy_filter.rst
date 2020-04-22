.. _config_network_filters_zookeeper_proxy:

ZooKeeper proxy
===============

The ZooKeeper proxy filter decodes the client protocol for
`Apache ZooKeeper <https://zookeeper.apache.org/>`_. It decodes the requests,
responses and events in the payload. Most opcodes known in
`ZooKeeper 3.5 <https://github.com/apache/zookeeper/blob/master/zookeeper-server/src/main/java/org/apache/zookeeper/ZooDefs.java>`_
are supported. The unsupported ones are related to SASL authentication.

.. attention::

   The zookeeper_proxy filter is experimental and is currently under active
   development. Capabilities will be expanded over time and the
   configuration structures are likely to change.

.. _config_network_filters_zookeeper_proxy_config:

Configuration
-------------

The ZooKeeper proxy filter should be chained with the TCP proxy filter as shown
in the configuration snippet below:

.. code-block:: yaml

  filter_chains:
  - filters:
    - name: envoy.filters.network.zookeeper_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.config.filter.network.zookeeper_proxy.v1alpha1.ZooKeeperProxy
        stat_prefix: zookeeper
    - name: envoy.filters.network.tcp_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.config.filter.network.tcp_proxy.v2.TcpProxy
        stat_prefix: tcp
        cluster: ...


.. _config_network_filters_zookeeper_proxy_stats:

Statistics
----------

Every configured ZooKeeper proxy filter has statistics rooted at *zookeeper.<stat_prefix>.* with the
following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  decoder_error, Counter, Number of times a message wasn't decoded
  request_bytes, Counter, Number of bytes in decoded request messages
  connect_rq, Counter, Number of regular connect (non-readonly) requests
  connect_readonly_rq, Counter, Number of connect requests with the readonly flag set
  ping_rq, Counter, Number of ping requests
  auth.<type>_rq, Counter, Number of auth requests for a given type
  getdata_rq, Counter, Number of getdata requests
  create_rq, Counter, Number of create requests
  create2_rq, Counter, Number of create2 requests
  setdata_rq, Counter, Number of setdata requests
  getchildren_rq, Counter, Number of getchildren requests
  getchildren2_rq, Counter, Number of getchildren2 requests
  remove_rq, Counter, Number of delete requests
  exists_rq, Counter, Number of stat requests
  getacl_rq, Counter, Number of getacl requests
  setacl_rq, Counter, Number of setacl requests
  sync_rq, Counter, Number of sync requests
  multi_rq, Counter, Number of multi transaction requests
  reconfig_rq, Counter, Number of reconfig requests
  close_rq, Counter, Number of close requests
  setwatches_rq, Counter, Number of setwatches requests
  checkwatches_rq, Counter, Number of checkwatches requests
  removewatches_rq, Counter, Number of removewatches requests
  check_rq, Counter, Number of check requests
  response_bytes, Counter, Number of bytes in decoded response messages
  connect_resp, Counter, Number of connect responses
  ping_resp, Counter, Number of ping responses
  auth_resp, Counter, Number of auth responses
  watch_event, Counter, Number of watch events fired by the server
  getdata_resp, Counter, Number of getdata responses
  create_resp, Counter, Number of create responses
  create2_resp, Counter, Number of create2 responses
  createcontainer_resp, Counter, Number of createcontainer responses
  createttl_resp, Counter, Number of createttl responses
  setdata_resp, Counter, Number of setdata responses
  getchildren_resp, Counter, Number of getchildren responses
  getchildren2_resp, Counter, Number of getchildren2 responses
  getephemerals_resp, Counter, Number of getephemerals responses
  getallchildrennumber_resp, Counter, Number of getallchildrennumber responses
  remove_resp, Counter, Number of remove responses
  exists_resp, Counter, Number of exists responses
  getacl_resp, Counter, Number of getacl responses
  setacl_resp, Counter, Number of setacl responses
  sync_resp, Counter, Number of sync responses
  multi_resp, Counter, Number of multi responses
  reconfig_resp, Counter, Number of reconfig responses
  close_resp, Counter, Number of close responses
  setauth_resp, Counter, Number of setauth responses
  setwatches_resp, Counter, Number of setwatches responses
  checkwatches_resp, Counter, Number of checkwatches responses
  removewatches_resp, Counter, Number of removewatches responses
  check_resp, Counter, Number of check responses

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
