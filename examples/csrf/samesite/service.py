import os

from aiohttp import web

routes = web.RouteTableDef()


@routes.post("/csrf/{status}")
async def csrf_with_status(request):
    return web.Response(text="Success!")


@routes.get("/")
async def get(request):
    file_dir = os.path.dirname(os.path.realpath(__file__))
    with open(f"{file_dir}/index.html") as f:
        return web.Response(text=f.read())


if __name__ == "__main__":
    app = web.Application()
    app.add_routes(routes)
    web.run_app(app, host='0.0.0.0', port=8080)
