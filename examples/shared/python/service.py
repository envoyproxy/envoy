from aiohttp import web

routes = web.RouteTableDef()


@routes.get("/")
async def get(request):
    return web.Response(text="Hello, World")


if __name__ == "__main__":
    app = web.Application()
    app.add_routes(routes)
    web.run_app(app, host='0.0.0.0', port=8080)
