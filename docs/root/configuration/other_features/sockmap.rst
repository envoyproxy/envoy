.. _config_sock_interface_sockmap:

Sockmap socket interface
========================

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.network.socket_interface.sockmap.v3.Sockmap>`

.. attention::

  The sockmap socket interface extension is experimental and is currently under active development.

.. note::

  This feature is only supported on Linux and requires a kernel 4.18 or later.

The sockmap socket interface accelerates same-host TCP hops by loading eBPF ``sock_ops`` and
``sk_msg`` programs that redirect payloads between local sockets through a
``BPF_MAP_TYPE_SOCKHASH``, bypassing the kernel TCP/IP stack. Connections whose peer is not on the
same host are absent from the map and transparently fall back to TCP/IP, so behavior is unchanged
for traffic that cannot be accelerated.

Loading and attaching the eBPF programs requires ``CAP_SYS_ADMIN``, or ``CAP_BPF`` and
``CAP_NET_ADMIN`` on newer kernels. When the programs cannot be loaded or attached, the interface
logs the failure and every socket falls back to the standard datapath, so traffic is never
interrupted.

Building Envoy with sockmap support
-----------------------------------

The eBPF datapath is only compiled when Envoy is built with ``--define=sockmap=enabled``, which
links ``libbpf``:

.. code-block:: bash

  bazel build --define=sockmap=enabled //source/exe:envoy-static

Default builds compile a no-op stub instead. The extension is still registered, so configuring
``bpf_program_path`` in a default build logs a warning and leaves every socket on the standard
datapath.

Compiling the eBPF object
-------------------------

Envoy does not ship a compiled eBPF object: the ``sock_ops`` and ``sk_msg`` programs and the user
space registration must share the same map name and key layout. The program source is part of this
extension at
:repo:`sockmap_kern.c <source/extensions/network/socket_interface/sockmap/bpf/sockmap_kern.c>`, and
Envoy provides a build rule that compiles it into ``sockmap_kern.o`` with ``clang``:

.. code-block:: bash

  bazel build //source/extensions/network/socket_interface/sockmap:sockmap_bpf

Compiling the object requires ``clang`` and the ``libbpf`` development headers on the host. Point
:ref:`bpf_program_path
<envoy_v3_api_field_extensions.network.socket_interface.sockmap.v3.Sockmap.bpf_program_path>` at the
resulting object, or at a custom build that exports the ``envoy_sockops`` and ``envoy_sk_msg``
programs and the ``envoy_sockhash`` map under those names with a matching key layout.

Example configuration
---------------------

Register the socket interface as a bootstrap extension and select it as the default socket
interface:

.. code-block:: yaml

  bootstrap_extensions:
  - name: envoy.extensions.network.socket_interface.sockmap
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.network.socket_interface.sockmap.v3.Sockmap
      bpf_program_path: /etc/envoy/sockmap_kern.o
  default_socket_interface: "envoy.extensions.network.socket_interface.sockmap"

The example accelerates proxy-to-proxy hops, which is the default. Accelerating application-to-proxy
hops additionally requires setting ``cgroup_path``.

How it works
------------

When ``bpf_program_path`` points at an object Envoy can load, the interface accelerates two kinds
of same-host hops, which can be enabled independently.

Application-to-proxy hops
~~~~~~~~~~~~~~~~~~~~~~~~~

When :ref:`cgroup_path
<envoy_v3_api_field_extensions.network.socket_interface.sockmap.v3.Sockmap.cgroup_path>` is set,
Envoy attaches the ``sock_ops`` program to that cgroup v2 directory. Every socket that reaches the
established state inside the cgroup is added to the ``sockhash``, which accelerates hops between
applications and Envoy that run in the same cgroup. Prefer a narrowly scoped cgroup over a broad
one such as the root. If ``cgroup_path`` is not set, the ``sock_ops`` program is not attached and
application sockets are not tracked.

Proxy-to-proxy hops
~~~~~~~~~~~~~~~~~~~

When :ref:`register_user_space_sockets
<envoy_v3_api_field_extensions.network.socket_interface.sockmap.v3.Sockmap.register_user_space_sockets>`
is true, which is the default, Envoy registers its accepted, connected, and duplicated sockets into
the ``sockhash`` from user space. This is independent of ``cgroup_path`` and accelerates
proxy-to-proxy hops on the same host without attaching the ``sock_ops`` program. The matching entry
is removed when the socket closes, so a later connection that reuses the tuple is never redirected
into a stale entry.

For either path, the ``sk_msg`` verdict program looks up the peer of each send in the ``sockhash``
and, when the peer is present, redirects the payload straight to its ingress queue with
``bpf_msg_redirect_hash``. Only IPv4 stream sockets are accelerated; other sockets, including IPv6
and Unix domain sockets, use the standard datapath unchanged. The ``sockhash`` holds one entry per
accelerated socket, up to :ref:`sockhash_max_entries
<envoy_v3_api_field_extensions.network.socket_interface.sockmap.v3.Sockmap.sockhash_max_entries>`.

.. note::

  When the programs load successfully, the interface logs ``sockmap acceleration enabled using
  <path>`` at the ``info`` level.
