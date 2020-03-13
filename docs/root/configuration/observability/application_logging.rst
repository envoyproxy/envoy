.. _config_application_logs:

Application logging
===================

Envoy and its filters write application logs for debuggability.
Envoy can be configured to output application logs in a format that is compatible with common log viewers.
This section documents how Envoy can be configured to enable integration with each log viewer.

Stackdriver Logging with GKE
----------------------------

`Stackdriver Logging <https://cloud.google.com/logging/>`_ can read logs from containers running on
`Google Kubernetes Engine <https://cloud.google.com/kubernetes-engine/>`_. Envoy should be configured
with the following :ref:`command line options <operations_cli>`:

* ``--log-format '%L%m%d %T.%e %t envoy] [%t][%n]%v'``: Logs are formatted in `glog <https://github.com/google/glog>`_
  format, allowing Stackdriver to parse the log severity and timestamp.
* ``--log-format-escaped``: Each string that is logged will be printed in a single line.
  C-style escape sequences (such as ``\n``) will be escaped and prevent a single string
  from spanning multiple lines. This ensures each log line is structured with the glog prefix.
* The ``--log-path`` flag **does not** need to be set, since Stackdriver can read logs from STDERR.
* The ``--log-level`` flag can be set to control the log severity logged to Stackdriver.

`Reference documentation <https://cloud.google.com/run/docs/logging#container-logs>`_ for Stackdriver on GKE.