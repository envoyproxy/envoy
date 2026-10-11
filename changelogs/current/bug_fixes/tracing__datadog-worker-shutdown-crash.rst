Fixed a crash during worker shutdown when the Datadog tracer is configured. A span still open when
the worker destroys its thread local objects, for example the span of an in-flight mirror request,
could flush its traces while the thread local cluster manager was being destroyed, and the request
sent to the collector crashed in ``ClusterManagerImpl::getThreadLocalCluster()`` or used a
destroyed collector cluster. The Datadog tracer now stops sending requests once its thread local
tracer is destroyed, and looks the collector cluster up for every report.
