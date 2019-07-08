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
      config:
        stat_prefix: zookeeper
    - name: envoy.tcp_proxy
      config:
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
