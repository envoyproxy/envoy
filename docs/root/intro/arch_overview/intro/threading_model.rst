.. _arch_overview_threading:

Threading model
===============

Envoy uses a "single process, multiple threads" architecture. This model is designed to be highly
concurrent and non-blocking. This allows a single Envoy process to efficiently handle a massive
number of active connections and requests.

High-Level Overview
-------------------

At a high level, the threading model consists of three main components:

1. **Main Thread**: A single thread that handles various (critical) coordination tasks. This
   includes configuration updates (xDS), stats flushing, and the administration interface. It
   does *not* handle high-volume traffic directly.
2. **Worker Threads**: A configurable number of threads (controlled by the ``--concurrency`` flag)
   that handle the actual listening, filtering, and forwarding of network traffic.
3. **File Flusher Thread**: A dedicated thread for flushing access logs to disk to avoid blocking
   the main processing path.

.. tip::
   For most workloads, we recommend configuring the number of worker threads to be equal to the
   number of hardware threads on the machine. This maximizes CPU utilization without incurring
   excessive context switching overhead.

Listener connection balancing
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When a connection is accepted by a listener, it is bound to a single worker thread for its entire
lifetime. This design means that Envoy's "hot path" is highly parallelized and it avoids complex
locking for the vast majority of request processing. In general, Envoy is written to be
non-blocking.

By default, there is no coordination between worker threads. This means that all worker threads
independently attempt to accept connections on each listener and rely on the kernel to perform
the balancing between threads.

For most workloads, the kernel does a very good job of balancing incoming connections. However, for
some workloads with a small number of very long lived connections (e.g., service mesh HTTP2/gRPC
egress), it might be desirable to have Envoy forcibly balance connections between worker threads.
To support this behavior, Envoy allows for different types of
:ref:`connection balancing
<envoy_v3_api_field_config.listener.v3.Listener.connection_balance_config>` to be configured on each
:ref:`listener <arch_overview_listeners>`.

.. note::
   On Windows the kernel is not able to balance the connections properly with the async IO model
   that Envoy is using.

   Until this is fixed by the platform, Envoy will enforce listener connection balancing on Windows.
   This allows us to balance connections between different worker threads. However, this behavior
   comes with a performance penalty.

Envoy's Threading Model for Developers
--------------------------------------

Dispatcher
^^^^^^^^^^

At the core of Envoy's threading model is the `Event::Dispatcher <https://github.com/envoyproxy/envoy/blob/main/envoy/event/dispatcher.h>`_. Each thread (Main and Worker)
runs a loop rooted in a ``Dispatcher``. This is a wrapper around ``libevent`` (or other event loops
in the future) that manages:

*   **File Descriptors**: Watching sockets for read/write events.
*   **Timers**: Scheduling tasks to run at a future time.
*   **Signals**: Handling OS signals (mainly on the Main Thread).

The Dispatcher allows code to be written in a single-threaded, non-blocking style. Instead of
blocking on I/O, you register a callback that triggers when the I/O is ready.

io_uring
^^^^^^^^

`io_uring <https://man7.org/linux/man-pages/man7/io_uring.7.html>`_ is an asynchronous I/O interface for Linux kernels that
can offer significant performance improvements over standard syscalls. Envoy supports using
``io_uring`` for specific I/O operations.

In Envoy's threading model, ``io_uring`` is integrated directly into the event loop of each worker
thread.

*   **Per-Thread Rings**: Each worker thread maintains its own independent ``io_uring`` instance
    (submission and completion queues). This adheres to Envoy's shared-nothing architecture and
    avoids locking contention between workers.
*   **Event Integration**: The ``io_uring`` completion queue is monitored via an ``eventfd`` which
    is registered with the thread's ``Event::Dispatcher``.
*   **Completion Processing**: When the kernel places a completion event in the queue, it signals
    the ``eventfd``. The Dispatcher wakes up, executes the callback, and processes the I/O
    completion just like any other file event.

This allows ``io_uring`` to coexist transparently with other event-driven mechanisms in Envoy.

Thread Local Storage (TLS)
^^^^^^^^^^^^^^^^^^^^^^^^^^

Envoy relies heavily on Thread Local Storage (TLS) to avoid locking contention. The
`ThreadLocal::Instance <https://github.com/envoyproxy/envoy/blob/main/envoy/thread_local/thread_local.h>`_ interface provides a mechanism to store data that is local to each thread
but accessible (mostly) via a global slot index.

*   **Allocating a Slot**: Code allocates a "slot" on the main thread. This slot acts as an index
    into a vector stored on every thread.
*   **Posting Updates**: When the main thread initiates a config update, it "posts" a closure to all
    worker threads. This closure runs on each worker, creating or updating the thread-local version
    of the data.
*   **O(1) Access**: At runtime, worker threads access their local data using the slot index, which
    is a fast O(1) vector lookup. This allows mechanisms like the Cluster Manager or Stats Store to
    be lock-free on the data path.

Main Thread Responsibilities
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The Main Thread receives special treatment. Its responsibilities include:

*   **xDS Processing**: It connects to the control plane, parses configuration updates, and orchestrates the update process across workers.
*   **Stats Flushing**: It periodically snapshots operational counters and flushes them to sinks (e.g., StatsD, Prometheus).
*   **Admin Listener**: It hosts the ``/admin`` endpoint logic.
*   **Process Signals**: Handles SIGTERM, SIGHUP, etc.

Exceptions and Extensions
^^^^^^^^^^^^^^^^^^^^^^^^^

While the core model is strict, some extensions may need their own threads.

*   **Async File I/O**: The `AsyncFileManagerThreadPool <https://github.com/envoyproxy/envoy/blob/main/source/extensions/common/async_files/async_file_manager_thread_pool.h>`_ manages a pool of threads to perform
    blocking file operations (like opening files or reading large bodies) without blocking the
    non-blocking worker threads.
*   **Cache Eviction**: The `CacheEvictionThread <https://github.com/envoyproxy/envoy/blob/main/source/extensions/http/cache/file_system_http_cache/cache_eviction_thread.h>`_ runs in the background to enforce cache size
    limits and evict old entries, preventing these expensive iterators from stalling the data path.

Development Patterns (Extension Guide)
--------------------------------------

If you are writing an Envoy extension, follow these patterns to ensure thread safety and performance.

Spawning Threads
^^^^^^^^^^^^^^^^

Do **not** use ``std::thread`` directly. Instead, use the `Thread::ThreadFactory <https://github.com/envoyproxy/envoy/blob/main/envoy/thread/thread.h>`_ available in the
`Server::Configuration::FactoryContext <https://github.com/envoyproxy/envoy/blob/main/envoy/server/factory_context.h>`_.

.. code-block:: cpp

   // Good: Uses Envoy's tracking and instrumented thread factory.
   Thread::ThreadPtr my_thread = thread_factory.createThread([this]() { doStuff(); });

   // BAD: Bypasses Envoy's thread tracking.
   std::thread my_thread([this]() { doStuff(); });

Using the factory ensures that:
*   The thread is registered with the ``Thread::ThreadId`` system.
*   It respects Envoy's signal handling and shutdown sequences.
*   It appears in crash dumps and debugging tools correctly.

Dispatcher Access & Usage
^^^^^^^^^^^^^^^^^^^^^^^^^

You will often need to execute code on a specific thread.

*   **From Main to Worker**: Use ``ThreadLocal::Instance::runOnAllThreads`` to execute a closure on every worker.
*   **Cross-Thread Posting**: Use ``Event::Dispatcher::post()``. This is thread-safe and allows you
    to queue a unit of work to be executed in the target thread's loop.

.. code-block:: cpp

   // Example: Posting a task to the main thread's dispatcher from a worker
   main_thread_dispatcher_->post([this]() {
     // This code runs on the main thread
     updateGlobalStats();
   });

Threading in Tests
^^^^^^^^^^^^^^^^^^

Envoy's testing philosophy prioritizes determinism. But alas, threaded code poses a challenge.

Unit Tests
""""""""""

For unit tests, the goal is to verify logic without the non-determinism of real threads.

*   **Mocking Threads**: Use `Thread::MockThreadFactory
    <https://github.com/envoyproxy/envoy/blob/main/test/mocks/thread/mocks.h>`_ instead of the real
    factory. This allows you to inspect what runnable was passed to the thread without spawning a
    system thread.
*   **Simulated Time**: Use `Event::SimulatedTimeSystem
    <https://github.com/envoyproxy/envoy/blob/main/test/test_common/simulated_time_system.h>`_ to
    control the flow of time and timer firing explicitly.

.. code-block:: cpp

  // Do this in your test fixture.
  NiceMock<Thread::MockThreadFactory> thread_factory_;
  EXPECT_CALL(thread_factory_, createThread(_)).WillOnce(Invoke([](std::function<void()> cb) {
    // Execute the callback immediately or store it for later to simulate thread timing.
    cb();
    return nullptr;
  }));

Integration Tests
"""""""""""""""""

Integration tests use real worker threads. To test them reliably, you must synchronize steps using
objects like ``absl::Notification``.

.. code-block:: cpp

  // Signal from the background thread.
  absl::Notification done;
  dispatcher_->post([&done]() {
    doWork();
    done.Notify();
  });

  // Wait on the main test thread
  done.WaitForNotification();

This is an extremely powerful way to reproduce (or prevent) race conditions.

Troubleshooting
---------------

Envoy provides several tools to help debug threading issues.

Watchdog
^^^^^^^^

Envoy includes a configurable "Watchdog" system. It spawns a separate thread that monitors the
liveness of the Main Thread and all Worker Threads.

*   **Mechanism**: Each monitored thread must "touch" a shared timestamp periodically. The watchdog thread scans these timestamps.
*   **Detection**: If a thread hasn't updated its timestamp within the configured interval (default
    200ms), the watchdog registers a "Miss". Extended blocking can trigger "MegaMiss" or even kill the
    process (fail-fast) to produce a core dump for analysis.
*   **Use Case**: This is primarily used to detect deadlocks or infinite loops in code.

Debug Assertions
^^^^^^^^^^^^^^^^

In debug builds (or when configured), Envoy employs thread-safety assertions.

*   ``ASSERT_IS_MAIN_OR_TEST_THREAD()``: Ensures the current code is running on the main thread.
*   ``ASSERT_IS_WORKER_THREAD()``: (If defined) Ensures code is running on the expected thread (Guard Dog thread).

Mutex Tracing
^^^^^^^^^^^^^

For performance debugging, Envoy supports mutex tracing (``--enable-mutex-tracing``). This allows
you to identify lock contention hotspots by recording hold times and wait times for contended
mutexes, viewable via the admin interface.
