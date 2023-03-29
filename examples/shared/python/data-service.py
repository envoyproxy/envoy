from aiohttp import web

routes = web.RouteTableDef()


@routes.get('/file.{suffix}')
async def get(request):
    suffix = request.match_info["suffix"]

    with open(f"/code/data/file.{suffix}") as f:
        if suffix == "txt":
            return web.Response(text=f.read())
        return web.json_response(body=f.read())


@routes.post("/upload")
async def post(request):
    data = await request.post()
    datalen = 0
    for k in data:
        datalen += len(k)
    resp = web.Response(text="OK")
    resp.headers["decompressed-size"] = str(datalen)
    return resp


if __name__ == "__main__":
    app = web.Application(client_max_size=1024**4)
    app.add_routes(routes)
    web.run_app(app, host='0.0.0.0', port=8080)
