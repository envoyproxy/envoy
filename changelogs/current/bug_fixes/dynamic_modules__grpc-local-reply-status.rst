Fixed the gRPC status of local replies sent by HTTP dynamic modules. ``send_response`` passed
``grpc-status: 0`` (OK) for every status code, so a gRPC client denied with, for example, a 403 was
told the call succeeded. A 200 reply still maps to OK; any other code now maps to the matching gRPC
status, as for other local replies (403 to ``PERMISSION_DENIED``, 429 to ``UNAVAILABLE``).
