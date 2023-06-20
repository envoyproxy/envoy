import logging
import os

from aiohttp import web

routes = web.RouteTableDef()


@routes.get("/{service_type}/{service}")
async def get(request):
    service = request.match_info["service"]
    print(f"Host: {request.headers.get('Host')}", flush=True)

    service_name = os.environ.get("SERVICE_NAME")

    if service_name and service != service_name:
        raise web.HTTPNotFound()

    return web.Response(text=f"Hello from behind Envoy (service {service})!\n")


if __name__ == "__main__":
    app = web.Application()
    logging.basicConfig(level=logging.DEBUG)
    app.add_routes(routes)
    web.run_app(app, host='0.0.0.0', port=8080)
