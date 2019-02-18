.. _config_network_filters_zookeeper_proxy:

ZooKeeper proxy
===============

The ZooKeeper proxy filter decodes the wire protocol between the ZooKeeper client
and server. It decodes the requests, responses and events in the payload.
The decoded info is emitted as dynamic metadata that can be combined with
access log filters to get detailed information on paths that are accessed
as well as the requests that are performed on each path.

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
  connect, Counter, Number of connect requests
  connect_readonly, Counter, Number of connect requests with the readonly flag set
  ping, Counter, Number of ping requests
  auth.<type>_rq, Counter, Number of auth requests for a given type
  getdata_rq, Counter, Number of getdata requests
  create_rq, Counter, Number of create requests
  create2_rq, Counter, Number of create2 requests
  setdata_rq, Counter, Number of setdata requests
  getchildren_rq, Counter, Number of getchildren requests
  getchildren2, Counter, Number of getchildren2 requests
  remove, Counter, Number of delete requests
  exists, Counter, Number of stat requests
  getacl, Counter, Number of getacl requests
  setacl, Counter, Number of setacl requests
  sync, Counter, Number of sync requests
  multi, Counter, Number of multi transaction requests
  reconfig, Counter, Number of reconfig requests
  close, Counter, Number of close requests
  setwatches, Counter, Number of setwatches requests

.. _config_network_filters_zookeeper_proxy_dynamic_metadata:

Dynamic Metadata
----------------

The ZooKeeper filter emits the following dynamic metadata for each message parsed:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  <path>, string, The path associated with the request or response or event.
