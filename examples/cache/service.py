from flask import Flask
from flask import request
from flask import make_response, abort
import yaml
import os
import datetime

app = Flask(__name__)


@app.route('/service/<service_number>/<response_id>')
def get(service_number, response_id):
    stored_response = yaml.safe_load(open('/etc/responses.yaml', 'r')).get(response_id)

    if stored_response is None:
        abort(404, 'No response found with the given id')

    response = make_response(stored_response.get('body') + '\n')
    if stored_response.get('headers'):
        response.headers.update(stored_response.get('headers'))

    # Generate etag header
    response.add_etag()

    # Append the date of response generation
    body_with_date = "{}\nResponse generated at: {}\n".format(
        response.get_data(as_text=True),
        datetime.datetime.utcnow().strftime("%a, %d %b %Y %H:%M:%S GMT"))

    response.set_data(body_with_date)

    # response.make_conditional() will change the response to a 304 response
    # if a 'if-none-match' header exists in the request and matches the etag
    return response.make_conditional(request)


if __name__ == "__main__":
    if not os.path.isfile('/etc/responses.yaml'):
        print('Responses file not found at /etc/responses.yaml')
        exit(1)
    app.run(host='0.0.0.0', port=8000)
