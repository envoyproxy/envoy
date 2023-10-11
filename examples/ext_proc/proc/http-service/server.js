const Http = require("http");
const path = require("path");

const tokens = require(process.env.USERS ||
  path.join(__dirname, "..", "users.json"));

const server = new Http.Server((req, res) => {
  const authorization = req.headers["authorization"] || "";
  const extracted = authorization.split(" ");
  if (extracted.length === 2 && extracted[0] === "Bearer") {
    const user = checkToken(extracted[1]);
    if (user !== undefined) {
      // The authorization server returns a response with "x-current-user" header for a successful
      // request.
      res.writeHead(200, { "x-current-user": user });
      return res.end();
    }
  }
  res.writeHead(403);
  res.end();
});

const port = process.env.PORT || 9002;
server.listen(port);
console.log(`starting HTTP server on: ${port}`);

function checkToken(token) {
  return tokens[token];
}
