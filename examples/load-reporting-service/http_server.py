from flask import Flask

app = Flask(__name__)


@app.route('/service')
def hello():
  return 'Hello from behind Envoy!'


if __name__ == "__main__":
  app.run(host='0.0.0.0', port=8082, debug=False)
