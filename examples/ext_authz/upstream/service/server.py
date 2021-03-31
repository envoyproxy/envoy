from flask import Flask, request

app = Flask(__name__)


@app.route('/service')
def hello():
    code = request.headers.get('x-code') or 200
    return 'Hello ' + request.headers.get('x-current-user') + ' from behind Envoy!', code


if __name__ == "__main__":
    app.run(host='0.0.0.0', port=8080, debug=False)
