#!/usr/bin/env python3

# Dummy Github-like repository website with OAuth

# NOTE: This is a partial and insecure implementation for testing only

# This is an implementation of a dummy OAuth provider for testing purposes only.
# Authorization is automatic on request - no real authorization or authentication
# is done.

import logging
import os
import pathlib
import secrets
import urllib.parse

from aiohttp import web

from shared import Data, debug_request, TokenStorage

# Demo envoy user, gets rid of rodents 8/
DEMOUSER = "envoydemo"

logger = logging.getLogger(__name__)

# TODO: add to app
# Note: You should not persist data in this way for any production system!
token_storage = TokenStorage(pathlib.Path(os.environ["TOKEN_STORAGE_PATH"]))


async def authorize(request):
    debug_request(request, "authorization")
    # Generate a random authorization code
    authorization_code = secrets.token_hex(16)
    logger.debug(f"Generated authorization code: {authorization_code}")
    # Store the authorization code
    token_storage[authorization_code] = {
        "user_id": DEMOUSER,
        "client_id": request.query["client_id"]
    }
    # Redirect the user back to the client with the authorization code
    state = urllib.parse.quote(request.query["state"], safe="")
    redirect_uri = f"{request.query['redirect_uri']}?code={authorization_code}&state={state}"
    logger.debug(f"Redirecting user: {redirect_uri}")
    return web.HTTPFound(redirect_uri)


async def authenticate(request):
    debug_request(request, "authentication")
    # Extract the authorization code from the request
    content = urllib.parse.parse_qs(await request.text())
    authorization_code = content.get("code")[0]
    logger.debug(f"Extracted authorization code: {authorization_code}")

    # Verify the authorization code
    if authorization_code not in token_storage:
        logger.debug(f"Authentication failed")
        return web.HTTPBadRequest(text="Invalid authorization code")

    # Generate an access token
    access_token = secrets.token_hex(16)
    logger.debug(f"Generated access token: {access_token}")

    # Store the access token and remove authorization code
    token_storage[access_token] = token_storage.pop(authorization_code)

    # Return the access token as JSON
    return web.json_response({"access_token": access_token, "token_type": "bearer"})


async def repo(request):
    debug_request(request, "repo")
    _data = Data(pathlib.Path(os.environ["DATA_PATH"]))
    user = _data["users"].get(request.match_info["user"])
    if not user:
        raise web.HTTPNotFound()
    repos = user.get("public_repos", {})
    if request.match_info["repo"] not in repos:
        raise web.HTTPNotFound()
    return web.Response(
        body=f"Myhub repo: {request.match_info['user']}/{request.match_info['repo']}")


async def user(request):
    debug_request(request, "user")
    _data = Data(pathlib.Path(os.environ["DATA_PATH"]))
    user = _data["users"].get(request.match_info["user"])
    if not user:
        raise web.HTTPNotFound()
    return web.Response(body=f"Myhub user: {request.match_info['user']}")


def main():
    logging.basicConfig(level=logging.DEBUG)
    app = web.Application()
    app.router.add_route("GET", '/authorize', authorize)
    app.router.add_route("POST", '/authenticate', authenticate)
    app.router.add_routes([web.static('/images', "/var/lib/myhub/images")])
    app.router.add_route("GET", '/users/{user}', user)
    app.router.add_route("GET", '/{user}/{repo}', repo)
    web.run_app(app, port=7000)


if __name__ == '__main__':
    main()
