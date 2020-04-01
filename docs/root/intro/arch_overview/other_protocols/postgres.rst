.. _arch_overview_postgres:

Postgres
==========

Envoy supports a network level Postgres sniffing filter to add network observability. By using the
Postgres proxy, Envoy is able to decode `Postgres frontend/backend protocol`_ and gather
statistics from the decoded information.

The main goal of the Postgres filter is to capture runtime statistics without impacting or
generating any load on the Postgres upstream server, it is transparent to it. The filter currently
offers the following features:

* Decrypt non SSL traffic, ignore SSL traffic.
* Decode session information.
* Capture transaction information, including commits and rollbacks.
* Basic decoding of the incoming SQL, exposing counters for different types of
  transactions (INSERTs, DELETEs, UPDATEs, etc).
* Count backend messages, distinguising OK messages, errors and warnings.

The Postgres filter solves a notable problem for Postgres deployments:
gathering this information either imposes additional load to the server; or
requires from pull-based querying metadata from the server, sometimes requiring
external components or extensions. This filter provides valuable observability
information, without impacting the performance of the upstream Postgres
server or requiring the installation of any software.

PostreSQL proxy filter :ref:`configuration reference <config_network_filters_postgres_proxy>`.

.. _Postgres frontend/backend protocol: https://www.postgres.org/docs/current/protocol.html
