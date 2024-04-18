#!/usr/bin/env python3

# Dummy Github-like API with OAuth

# NOTE: This is a partial and insecure implementation for testing only

import json
import logging
import os
import pathlib
import secrets
import urllib.parse

import yaml

from aiohttp import web

from shared import Data, debug_request, TokenStorage

logger = logging.getLogger(__name__)
MYHUB_URL = os.environ.get("MYHUB_URL") or "http://localhost:7000"

# TODO: add to app
# Note: You should not persist data in this way for any production system!
token_storage = TokenStorage(pathlib.Path(os.environ["TOKEN_STORAGE_PATH"]))


async def user(request):
    debug_request(request, "user")
    _data = Data(pathlib.Path(os.environ["DATA_PATH"]))
    access_token = request.cookies["BearerToken"]
    if access_token not in token_storage:
        raise web.HTTPForbidden()
    user_id = token_storage[access_token]["user_id"]
    user = _data["users"][user_id]
    user["avatar_url"] = f"{MYHUB_URL}/images{user['avatar_url']}"
    for resource in ["public_repos", "followers", "following"]:
        user[resource] = len(user[resource])
    return web.json_response(user, dumps=_dumps)


async def resources(request):
    resource_type = request.match_info["resource"]
    debug_request(request, resource_type)
    _data = Data(pathlib.Path(os.environ["DATA_PATH"]))
    access_token = request.cookies.get("BearerToken")
    allowed = (
        access_token
        and (token_storage.get(access_token, {}).get("user_id") == request.match_info["user"]))
    if not allowed:
        raise web.HTTPForbidden()
    user = _data["users"].get(request.match_info["user"])
    if not user:
        raise web.HttpNotFound()
    if resource_type == "repos":
        resources = [
            dict(
                html_url=f"{MYHUB_URL}/{user['login']}/{resource}",
                updated_at=_data["repos"][resource]["updated_at"],
                full_name=f"{user['login']}/{resource}") for resource in user["public_repos"]
        ]
    elif resource_type in ["followers", "following"]:
        resources = [
            dict(
                avatar_url=f"{MYHUB_URL}/images{_data['users'][related_user]['avatar_url']}",
                name=_data['users'][related_user]["name"],
                html_url=f"{MYHUB_URL}/users/{related_user}",
                login=related_user) for related_user in user[resource_type]
        ]
    else:
        raise web.HTTPNotFound()
    return web.json_response(resources, dumps=_dumps)


def _dumps(s):
    return json.dumps(s, separators=(',', ':'))


def main():
    logging.basicConfig(level=logging.DEBUG)
    app = web.Application()
    app.router.add_route("GET", '/user', user)
    app.router.add_route("GET", '/users/{user}/{resource}', resources)
    web.run_app(app, port=7000)


if __name__ == '__main__':
    main()
