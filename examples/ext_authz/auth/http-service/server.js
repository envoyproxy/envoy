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
      res.writeHead(200);
      return res.end();
    }
  }
  res.writeHead(403);
  res.end();
});

server.listen(process.env.PORT || 8080);

function checkToken(token) {
  return tokens[token];
}
