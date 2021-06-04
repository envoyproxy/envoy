from flask import Flask

app = Flask(__name__)


@app.route('/cors/<status>')
def cors_enabled(status):
    return 'Success!'


if __name__ == "__main__":
    app.run(host='0.0.0.0', port=8000)
