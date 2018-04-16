.. _config_cluster_manager_cluster_ring_hash_lb_config:

Ring hash load balancer configuration
=====================================

Ring hash load balancing settings are used when the *lb_type* is set to *ring_hash* in the
:ref:`cluster manager <config_cluster_manager_cluster_lb_type>`.

.. code-block:: json

  {
    "minimum_ring_size": "...",
    "use_std_hash": "..."
  }

minimum_ring_size
  *(optional, integer)* Minimum hash ring size, i.e. total virtual nodes. A larger size will provide
  better request distribution since each host in the cluster will have more virtual nodes. Defaults
  to 1024. In the case that total number of hosts is greater than the minimum, each host will be
  allocated a single virtual node.

use_std_hash
  *(optional, boolean)* Defaults to true, meaning that std::hash is used to hash hosts onto the
  ketama ring. std::hash can vary by platform. For this reason, Envoy will eventually use
  `xxHash <https://github.com/Cyan4973/xxHash>`_ by default. This field exists for migration
  purposes and will eventually be deprecated. Set it to false to use xxHash now.
