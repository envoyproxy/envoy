``log*()``
^^^^^^^^^^

.. code-block:: lua

  handle:logTrace(message)
  handle:logDebug(message)
  handle:logInfo(message)
  handle:logWarn(message)
  handle:logErr(message)
  handle:logCritical(message)

Logs a message using Envoy's application logging. *message* is a string to log.

These are supported on all objects that Envoy exposes to Lua.
