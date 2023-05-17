from aiohttp import web

routes = web.RouteTableDef()


@routes.get("/cors/{status}")
async def get(request):
    return web.Response(text="Success!")


if __name__ == "__main__":
    app = web.Application()
    app.add_routes(routes)
    web.run_app(app, host='0.0.0.0', port=8080)
