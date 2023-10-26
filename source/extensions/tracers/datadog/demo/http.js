// This is an HTTP server that listens on port 8080 and responds to all
// requests with some text, including the request headers as JSON.

const http = require('http');
const process = require('process');

// In order for the span(s) associated with an HTTP request to be considered
// finished, the body of the response corresponding to the request must have
// ended.
function ignoreRequestBody(request) {
  function ignore() {}
  request.on('data', ignore);
  request.on('end', ignore);
}

function requestListener (request, response) {
  ignoreRequestBody(request);
  const responseBody = JSON.stringify({
    "service": "http",
    "headers": request.headers
  }, null, 2);
  console.log(responseBody);
  response.end(responseBody);
}

console.log('http node.js web server is running');
const server = http.createServer(requestListener);
server.listen(8080);

process.on('SIGTERM', function () {
  console.log('Received SIGTERM');
  server.close(function () { process.exit(0); });
});
