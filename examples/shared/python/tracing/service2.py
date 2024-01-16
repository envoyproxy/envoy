import logging
import os
import socket
import sys

import aiohttp
from aiohttp import web

routes = web.RouteTableDef()

TRACE_HEADERS_TO_PROPAGATE = [
    'X-Ot-Span-Context',
    'X-Request-Id',

    # Zipkin headers
    'X-B3-TraceId',
    'X-B3-SpanId',
    'X-B3-ParentSpanId',
    'X-B3-Sampled',
    'X-B3-Flags',

    # Jaeger header (for native client)
    "uber-trace-id",

    # SkyWalking headers.
    "sw8"
]


@routes.get("/{service_type}/{service}")
async def get(request):
    service_type = request.match_info["service_type"]
    service = request.match_info["service"]
    service_name = os.environ.get("SERVICE_NAME")

    if service_name and service != service_name:
        raise web.HTTPNotFound()

    if service_type == "trace" and int(service_name) == 1:
        # call service 2 from service 1
        headers = {}
        for header in TRACE_HEADERS_TO_PROPAGATE:
            if header in request.headers:
                headers[header] = request.headers[header]
        async with aiohttp.ClientSession() as session:
            async with session.get("http://localhost:9000/trace/2", headers=headers) as resp:
                pass

    return web.Response(
        text=(
            f"Hello from behind Envoy (service {service})! "
            f"hostname {socket.gethostname()} "
            f"resolved {socket.gethostbyname(socket.gethostname())}\n"))


if __name__ == "__main__":
    app = web.Application()
    logging.basicConfig(level=logging.DEBUG)
    app.add_routes(routes)
    web.run_app(app, host='0.0.0.0', port=8080)
