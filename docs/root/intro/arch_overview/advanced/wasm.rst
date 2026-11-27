.. _arch_overview_wasm:

Wasm
====

Envoy supports execution of Wasm plugins: the Wasm modules written against the `Proxy-Wasm specification
<https://github.com/proxy-wasm/spec>`_, which defines an extension interface through which the modules can implement
custom extension logic. Currently, the `Proxy-Wasm ABI (Application Binary Interface) version 0.2.1
<https://github.com/proxy-wasm/spec/tree/main/abi-versions/v0.2.1>`_ is recommended. Wasm offers a portable and compact
binary executable format that can be compiled once from a variety of languages (`C++
<https://github.com/proxy-wasm/proxy-wasm-cpp-sdk>`_, `Rust <https://github.com/proxy-wasm/proxy-wasm-rust-sdk>`_) and
run anywhere. Please see the :ref:`sandbox example <install_sandboxes_wasm_filter>` for instructions on how to create
and deploy a Wasm module.

Envoy provides several extension points at which Wasm plugins can be invoked:

* As an :ref:`HTTP filter <envoy_v3_api_msg_extensions.filters.http.wasm.v3.Wasm>`
* As a :ref:`network-level (L4) filter <envoy_v3_api_msg_extensions.filters.network.wasm.v3.Wasm>`
* As a :ref:`StatsSink <envoy_v3_api_msg_extensions.stat_sinks.wasm.v3.Wasm>`
* As an :ref:`AccessLogger <envoy_v3_api_msg_extensions.access_loggers.wasm.v3.WasmAccessLog>`
* As a :ref:`background service <envoy_v3_api_msg_extensions.wasm.v3.WasmService>`

The particular functions that Envoy invokes on a Wasm plugin depend on the extension point at which it is configured.

Contexts, plugins, and VMs
--------------------------

A Wasm context refers to the interface implementation between the host (Envoy) and the guest (Wasm bytecode). For an
HTTP filter, there are two types of contexts: a *root context* (context with ID 0) represents the configuration-only
context, and a *per-request context* that is created once per each request.

A Wasm VM refers to the execution instance of a Wasm bytecode module. Multiple VMs can be created for the same bytecode
when using the ``vm_id`` field, and they each would have separate per-VM memory. The opposite is also true, the same VM
can contain multiple implementations for different extensions (plugins), to be used in different contexts. VM sharing
optimizes the runtime overhead and reduces the total binary size of the bytecode since the extensions can share the
code.

A Wasm plugin is the link between the context and the VM. It identifies which Wasm VM to use for a context, which
extension to invoke within the Wasm VM execution instance (via the :ref:`root_id
<envoy_v3_api_field_extensions.wasm.v3.PluginConfig.root_id>` field value), and how to route the configuration call to
the extension during the xDS update.


Execution model for filters
---------------------------

At runtime, the Wasm modules are executed inline in the worker threads via a series of stream callbacks as defined by
the ABI. The worker threads operate independently and do not share the Wasm execution instances and their runtime
memory. Asynchronous or blocking operations are delegated to Envoy to perform; upon completion, Envoy then invokes a
separate completion callback on the plugin.

At configuration time, the Wasm modules are loaded into the Wasm engine on the main thread. A separate Wasm execution
instance is spawned for each combination of the module binary and the :ref:`vm_id
<envoy_v3_api_field_extensions.wasm.v3.VmConfig.vm_id>` field value. The instance receives the Wasm configuration for
each plugin via a callback, possibly repeatedly for each xDS listener encapsulating the Wasm filter. If the callback
accepts the configuration, the main execution instance is cloned to each worker thread.

Note that this configuration model is distinct for the Wasm filter. Unlike a regular HTTP filter that is instantiated
independently for each xDS listener, xDS listeners share the same main Wasm execution instance across xDS updates.

Envoy Attributes
----------------

Wasm ABI exposes Envoy-specific host attributes via the dedicated `proxy_get_property
<https://github.com/proxy-wasm/spec/tree/main/abi-versions/v0.2.1#proxy_get_property>`_ interface stub. These are the
standard :ref:`attributes <arch_overview_attributes>` and the values are returned via the type-specific binary
serialization.

.. _arch_overview_wasm_foreign_functions:

Foreign functions
-----------------

Envoy offers additional functionality over the Proxy-Wasm ABI via `proxy_call_foreign_function
<https://github.com/proxy-wasm/spec/tree/main/abi-versions/v0.2.1#proxy_call_foreign_function>`_ binary interface:

* ``sign`` creates cryptographic signatures.
* ``verify_signature`` verifies cryptographic signatures.
* ``compress`` applies ``zlib`` compression.
* ``uncompress`` applies ``zlib`` decompression.
* ``declare_property`` creates a placeholder filter state object with the type information.
* ``set_envoy_filter_state`` set a filter state object.
* ``clear_route_cache`` updates the selected route.
* ``expr_create`` compiles a CEL expression for evaluation.
* ``expr_evalute`` evaluates a compiled CEL expression.
* ``expr_delete`` deletes a compiled CEL expression.
* ``redis_call`` issues a Redis command against a cluster that speaks RESP2 (directly, or via an
  ``envoy_internal`` address naming an internal listener), e.g. a :ref:`Redis proxy
  <config_network_filters_redis_proxy>` network filter. See :ref:`Redis calls
  <arch_overview_wasm_redis_call>` below for the full argument/result wire format.

.. _arch_overview_wasm_redis_call:

Redis calls
~~~~~~~~~~~

The ``redis_call`` foreign function lets a plugin issue a Redis command without speaking RESP2
framing or managing connections itself. Envoy acquires a connection from a generic
:ref:`Tcp::ConnectionPool <arch_overview_conn_pool>` for the target cluster, writes the command
using the same RESP2 encoder/decoder as the :ref:`Redis proxy
<config_network_filters_redis_proxy>` network filter, and releases the connection back to the pool
once the single reply has been read. It is deliberately thin: no key-based shard hashing, Redis
Cluster MOVED/ASK redirection, or pipelining -- one request and one reply per call.

The call is split into two parts, the same split as every other asynchronous foreign function:

1. The foreign function call itself is synchronous and only starts the Redis request; it returns
   immediately with either an error, or a token identifying where the eventual result will be
   delivered.
2. The result is delivered later, asynchronously, via the Proxy-Wasm shared-queue mechanism
   (``proxy_dequeue_shared_queue`` / ``proxy_on_queue_ready``), **always on the plugin's root
   context** -- never on whichever context (e.g. a per-request context) actually issued the call.

Both the argument and result payloads are Protobuf messages
(``envoy.source.extensions.common.wasm.RedisCallArguments`` / ``RedisCallStartResult`` /
``RedisCallResult``), serialized the same way as every other foreign function argument/result.
These protos live under ``source/`` rather than in a published API or SDK package, so a module
must bundle its own copy of (or hand-decode) the wire format described below.

**Supported commands**

``redis_call`` is a denylist, not an allowlist. The denylist exists because the connection is
released back to the pool after one reply, so a command that mutates per-connection state, or that
can produce more than one reply, could leak onto an unrelated later call that reuses the same
connection. It is **not** a privilege boundary -- destructive/administrative commands like
``FLUSHALL`` are not denylisted, since a Wasm module is operator-configured. Rejected by command
name (case-insensitively), before any connection is acquired:

* ``AUTH``, ``HELLO``, ``SELECT``, ``RESET`` -- change per-connection auth/protocol/DB state.
* ``MULTI``, ``EXEC``, ``DISCARD``, ``WATCH``, ``UNWATCH`` -- transaction state is per-connection
  and can't span a ``redis_call``'s single round trip.
* ``SUBSCRIBE``/``UNSUBSCRIBE`` (and variants), ``MONITOR`` -- produce an unbounded stream of
  pushed replies.
* ``QUIT`` -- closes the connection after its reply, which would look like a normal call while
  releasing a dead connection back to the pool.
* ``CLIENT`` -- rejected wholesale, since subcommands like ``TRACKING``/``REPLY``/``PAUSE`` mutate
  per-connection state.
* ``READONLY``, ``READWRITE``, ``ASKING`` -- alter per-connection cluster-routing mode.
* ``SYNC``, ``PSYNC``, ``REPLCONF`` -- enter unbounded replication streaming.

Blocking commands (``BLPOP``, ``BRPOP``, ``WAIT``, etc.) are **not** denylisted, but tie up a
pooled connection for however long they block, up to ``timeout_millis`` below.

**Request** (``RedisCallArguments``):

``cluster``
  Name of the cluster to send the command to. Must resolve to something that speaks RESP2 on its
  downstream-facing side -- directly, or via an ``envoy_internal`` address naming an internal
  listener (e.g. a :ref:`Redis proxy <config_network_filters_redis_proxy>` network filter).
  ``redis_call`` does not implement its own cluster-aware host/shard selection, auth, or
  redirection handling.

``command_args``
  The Redis command and its arguments, e.g. ``["GET", "mykey"]``. Must be non-empty and not name a
  denylisted command. No size cap beyond the calling module's own linear memory.

``timeout_millis``
  Mandatory, bounded to 5 minutes. Destroying the originating context does not cancel the call --
  ``timeout_millis`` is the only bound on how long it may outlive that context.

There is no dedicated concurrency limit on outstanding calls: backpressure comes from the target
cluster's own circuit breakers, the same as ``httpCall``/``grpcCall``. The byte budget described
under ``WasmResult::InternalFailure`` below bounds a different thing: the real size of results that
have completed but not yet been drained off the shared queue. An outstanding call isn't charged
anything against that budget, since it holds no reserved buffer.

.. _arch_overview_wasm_redis_call_backpressure:

The foreign function call returns:

* ``WasmResult::Ok`` with a serialized ``RedisCallStartResult`` on success.
* ``WasmResult::BadArgument`` if ``command_args`` is empty or rejected, ``timeout_millis`` is zero
  or exceeds 5 minutes, or the cluster doesn't exist/has no connection pool.
* ``WasmResult::InternalFailure`` if too many results are already completed but undrained on the
  shared queue, against an aggregate byte budget (100 MiB) of their real serialized size. Scoped
  per plugin instance and tracked across reconfiguration.

**Start result** (``RedisCallStartResult``), returned synchronously:

``queue_token``
  The shared-queue token to dequeue the result from. Stable for every ``redis_call`` made under
  the same root context, so a plugin registers ``onQueueReady`` interest once regardless of how
  many calls it makes. Not a per-call token -- results from concurrent calls can interleave, so a
  module must dequeue in a loop until empty on every ``onQueueReady``.

``call_id``
  Identifies this call's eventual ``RedisCallResult`` among any others interleaved on the queue.

**Result** (``RedisCallResult``), delivered later by enqueuing onto ``queue_token``:

``status``
  ``Ok`` or ``Failure``. Failure covers connection/pool errors, a malformed reply, a reply that
  arrives after the one expected (which also closes the connection instead of returning it to the
  pool), a timeout, and a reply exceeding the 10 MiB re-encoded-size cap -- enforced cumulatively
  against wire bytes received, not only once a reply finishes decoding. That cap bounds wire bytes,
  not the RESP decoder's own transient allocation while decoding an aggregate's header, which the
  decoder bounds separately via its own element-count/nesting limits.

``response``
  On ``Ok``, the reply re-encoded as RESP2 regardless of whether the peer spoke RESP2 or RESP3.
  RESP2's null array and null bulk string forms aren't distinguished internally, so a null reply is
  always re-encoded as the null bulk string (``$-1\r\n``). Empty on ``Failure``.

``call_id``
  Matches the ``call_id`` from this call's ``RedisCallStartResult``.

``origin_context_id``
  The ``id()`` of the context that issued the ``redis_call``, used to route the result back since
  results are always delivered on the root context. That context may no longer exist by the time
  the result arrives, so a module must track request-scoped state by this id, not by the context
  itself.

A plugin using ``redis_call`` must implement ``proxy_on_queue_ready`` on its root context, the only
context the shared-queue contract ever invokes it on. A minimal, correct implementation (C++ SDK):

.. code-block:: c++

  void MyRootContext::onQueueReady(uint32_t token) {
    // A single onQueueReady invocation is not guaranteed to correspond to exactly one result;
    // drain the queue in a loop until it's empty.
    WasmDataPtr data;
    while (dequeueSharedQueue(token, &data) == WasmResult::Ok) {
      RedisCallResult result;
      if (!result.ParseFromString(data->toString())) {
        continue;
      }
      // onQueueReady always runs on the root context, never on the stream-level Context that
      // issued the call, so route back to it via origin_context_id. getContext() returns nullptr
      // if that context no longer exists (e.g. the downstream stream already ended) -- in that
      // case there's nothing to resume, so just drop the result.
      if (Context* origin = getContext(result.origin_context_id())) {
        // setEffectiveContext() makes subsequent host-API calls (continueStream(),
        // sendLocalResponse(), etc.) act on origin instead of the running root context.
        origin->setEffectiveContext();
        static_cast<MyFilterContext*>(origin)->onRedisCallResult(result);
      }
    }
  }

``MyFilterContext::onRedisCallResult`` is plugin-specific: it's whatever the stream-level context
needs to do with the result (resume a paused request, write a header, etc.). The one thing every
implementation must do itself is track its own per-``call_id`` state across the call -- there's no
host-side mechanism for this, since ``RedisCallResult`` only carries ``call_id`` and
``origin_context_id``, not whatever the module was doing when it issued the call.

A call still outstanding when reconfiguration replaces its root context is delivered to the
*replacement* root, not lost: the old root resolves it as ``Failure`` during shutdown, and the new
root re-targets the shared queue and drains that (and any other waiting) result on startup. A
module never needs to guard against reconfiguration itself, but should expect any call outstanding
at that moment to resolve as ``Failure``.

Wasm runtime
------------

Envoy Wasm can be :ref:`configured <envoy_v3_api_field_extensions.wasm.v3.VmConfig.runtime>` to use one of several Wasm
runtime implementations: ``V8``, ``WAMR``, or ``Wasmtime``, as long as the runtime is included in the Envoy
distribution.  There is also a special pseudo-Wasm runtime, called the "Null VM", in which Wasm plugin code is compiled
to native (non-Wasm) code and statically linked directly into the Envoy binary.

