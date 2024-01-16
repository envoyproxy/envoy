.. _arch_overview_postgres:

Postgres
========

Envoy supports a network level Postgres sniffing filter to add network observability. By using the
Postgres proxy, Envoy is able to decode `Postgres frontend/backend protocol`_ and gather
statistics from the decoded information.

The main goal of the Postgres filter is to capture runtime statistics without impacting or
generating any load on the Postgres upstream server, it is transparent to it. The filter currently
offers the following features:

* Decode non SSL traffic, ignore SSL traffic.
* Decode session information.
* Encode incoming non SSL traffic before forwarding upstream.
* Capture transaction information, including commits and rollbacks.
* Expose counters for different types of statements (INSERTs, DELETEs, UPDATEs, etc).
  The counters are updated based on decoding backend CommandComplete messages not by decoding SQL statements sent by a client.
* Count frontend, backend and unknown messages.
* Identify errors and notices backend responses.

The Postgres filter solves a notable problem for Postgres deployments:
gathering this information either imposes additional load to the server; or
requires pull-based querying for metadata from the server, sometimes requiring
external components or extensions. This filter provides valuable observability
information, without impacting the performance of the upstream Postgres
server or requiring the installation of any software.

Postgres proxy filter :ref:`configuration reference <config_network_filters_postgres_proxy>`.

.. _Postgres frontend/backend protocol: https://www.postgresql.org/docs/current/protocol.html
