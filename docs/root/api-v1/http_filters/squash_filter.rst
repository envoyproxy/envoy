.. _config_http_filters_squash_v1:

Squash
======

Squash :ref:`configuration overview <config_http_filters_squash>`.

.. code-block:: json

  {
    "name": "squash",
    "config": {
      "cluster": "...",
      "attachment_template": "{...}",
      "attachment_timeout_ms": "...",
      "attachment_poll_period_ms": "...",
      "request_timeout_ms": "..."
    }
  }

cluster
  *(required, object)* The name of the cluster that hosts the Squash server.

attachment_template
  *(required, object)* When the filter requests the Squash server to create a DebugAttachment, it
  will use this structure as template for the body of the request. It can contain reference to
  environment variables in the form of '{{ ENV_VAR_NAME }}'. These can be used to provide the Squash
  server with more information to find the process to attach the debugger to. For example, in a
  Istio/k8s environment, this will contain information on the pod:

  .. code-block:: json

   {
     "spec": {
       "attachment": {
         "pod": "{{ POD_NAME }}",
         "namespace": "{{ POD_NAMESPACE }}"
       },
       "match_request": true
     }
   }

  (where POD_NAME, POD_NAMESPACE are configured in the pod via the Downward API)

request_timeout_ms
  *(required, integer)* The timeout for individual requests sent to the Squash cluster. Defaults to
  1 second.

attachment_timeout_ms
  *(required, integer)* The total timeout Squash will delay a request and wait for it to be
  attached. Defaults to 60 seconds.

attachment_poll_period_ms
  *(required, integer)* Amount of time to poll for the status of the attachment object in the Squash
  server (to check if has been attached). Defaults to 1 second.

