# Generate direct_response routes for a passed in file list
# Expects an xDS config to be passed as the `base` argument to
# inject the routes into.

def getMimeType:
    {".js": "text/javascript",
     ".jpg": "image/jpeg",
     ".jpeg": "image/jpeg",
     ".png": "image/png",
     ".gif": "image/gif",
     ".svg": "image/svg+xml",
     ".ico": "image/x-icon",
     ".css": "text/css",
     ".html": "text/html",
     ".txt": "text/plain",
     ".xml": "application/xml",
     ".json": "application/json",
     ".pdf": "application/pdf",
     ".zip": "application/zip",
     ".tar": "application/x-tar",
     ".gz": "application/gzip"
    }[match("\\.[^.]*$").string // "application/octet-stream"]
;

def pathToDirectResponse:
  . as $path
  | sub("^dist/"; "/") as $asset
  | $asset
  | sub("^/index.html$"; "/") as $path
  | if $path == "/" then
      {prefix: $path}
    else {$path} end
  | {match: .,
     direct_response: {
         status: 200,
         body: {filename: "/var/www/html\($asset)"}
     },
     response_headers_to_add: [
         {header: {
              key: "Content-Type",
              value: ($asset | getMimeType)}}]}
;

split("\n") as $assets
| ($base | fromjson) as $base
| $assets
| map(select(. != "") | pathToDirectResponse) as $routes
| $base
| .resources[0].filter_chains[0].filters[0].typed_config.route_config.virtual_hosts[0].routes = $routes
