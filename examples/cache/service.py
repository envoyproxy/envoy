import hashlib
import os
import re
import datetime
from typing import Optional

import yaml

from aiohttp import web

routes = web.RouteTableDef()

# TOOD(phlax): move this to pytooling

# Etag fun lifted from https://github.com/zhangkaizhao/aiohttp-etag


def _check_etag_header(request, computed_etag) -> bool:
    # Find all weak and strong etag values from If-None-Match header
    # because RFC 7232 allows multiple etag values in a single header.
    etags = re.findall(r'\*|(?:W/)?"[^"]*"', request.headers.get("If-None-Match", ""))
    if not etags:
        return False

    match = False
    if etags[0] == "*":
        match = True
    else:
        # Use a weak comparison when comparing entity-tags.
        def val(x: str) -> str:
            return x[2:] if x.startswith("W/") else x

        for etag in etags:
            if val(etag) == val(computed_etag):
                match = True
                break
    return match


def _compute_etag(body) -> str:
    hasher = hashlib.sha1()
    hasher.update(body.encode())
    return f'"{hasher.hexdigest()}"'


def _set_etag_header(response, computed_etag) -> None:
    response.headers["Etag"] = computed_etag


@routes.get("/service/{service_number}/{response_id}")
async def get(request):
    service_number = request.match_info["service_number"]
    response_id = request.match_info["response_id"]
    stored_response = yaml.safe_load(open('/etc/responses.yaml', 'r')).get(response_id)

    if stored_response is None:
        raise web.HTTPNotFound(reason="No response found with the given id")

    # Etag is computed for every response, which only depends on the response body in
    # the yaml file (i.e. the appended date is not taken into account).
    body = stored_response.get('body')
    computed_etag = _compute_etag(body)

    if _check_etag_header(request, computed_etag):
        return web.HTTPNotModified(headers={'ETag': computed_etag})

    request_date = datetime.datetime.utcnow().strftime("%a, %d %b %Y %H:%M:%S GMT")
    response = web.Response(text=f"{body}\nResponse generated at: {request_date}\n")

    if stored_response.get('headers'):
        response.headers.update(stored_response.get('headers'))

    _set_etag_header(response, computed_etag)

    return response


if __name__ == "__main__":
    if not os.path.isfile('/etc/responses.yaml'):
        print('Responses file not found at /etc/responses.yaml')
        exit(1)
    app = web.Application()
    app.add_routes(routes)
    web.run_app(app, host='0.0.0.0', port=8080)
