from aiohttp import web

# TODO(phlax): shift to aiopg
import psycopg2

routes = web.RouteTableDef()


@routes.get("/")
async def get(request):
    conn = psycopg2.connect("host=postgres user=postgres")
    cur = conn.cursor()
    cur.execute('SELECT version()')
    msg = 'Connected to Postgres, version: %s' % cur.fetchone()
    cur.close()
    return web.Response(text=msg)


if __name__ == "__main__":
    app = web.Application()
    app.add_routes(routes)
    web.run_app(app, host='0.0.0.0', port=8080)
