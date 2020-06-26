from flask import Flask, request

app = Flask(__name__)


@app.route('/service')
def hello():
  return 'Hello ' + request.headers.get('x-current-user') + ' from behind Envoy!'


if __name__ == "__main__":
  app.run(host='0.0.0.0', port=8080, debug=False)
