import logging
import os

from aiohttp import web

routes = web.RouteTableDef()
healthy = True


@routes.get("/")
async def get(request):
    global healthy
    if healthy:
        return web.Response(text=f"Hello from {os.environ['HOST']}!\n")
    else:
        raise web.HTTPServiceUnavailable(reason="Unhealthy")


@routes.get("/healthy")
async def healthy(request):
    global healthy
    healthy = True
    return web.Response(text=f"[{os.environ['HOST']}] Set to healthy\n", status=201)


@routes.get("/unhealthy")
async def unhealthy(request):
    global healthy
    healthy = False
    return web.Response(text=f"[{os.environ['HOST']}] Set to unhealthy\n", status=201)


if __name__ == "__main__":
    app = web.Application()
    logging.basicConfig(level=logging.DEBUG)
    app.add_routes(routes)
    web.run_app(app, host='0.0.0.0', port=8080)
