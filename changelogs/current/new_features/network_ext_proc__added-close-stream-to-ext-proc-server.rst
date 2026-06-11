Added ``close_stream_to_ext_proc_server`` to :ref:`ProcessingResponse
<envoy_v3_api_msg_service.network_ext_proc.v3.ProcessingResponse>` to allow the external processor to request
closing the gRPC stream early, causing subsequent data to bypass the network ``ext_proc`` filter.
