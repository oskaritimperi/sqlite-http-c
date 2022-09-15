import http.server
import subprocess
import threading
import sqlite3
import json

tests = (
    {
        "request_url": "http://localhost:8080/foobar",
        "request_method": "GET",
        "response_body": "foobar",
        "response_headers": "Content-Size: 6\r\n",
        "response_status_code": 200,
    },
)

test_idx = 0

class Handler(http.server.BaseHTTPRequestHandler):
    def version_string(self):
        return "testclient/1.0"

    def date_time_string(self, timestamp=None):
        return "Mon, 05 Sep 2022 19:26:53 GMT"

    def do_GET(self):
        self._handle_test("GET")

    def do_POST(self):
        self._handle_test("POST")

    def _handle_test(self, method):
        global test_idx
        data = tests[test_idx]
        test_idx += 1

        try:
            if method != data["request_method"]:
                self.send_response(400)
                self.end_headers()
            else:
                self.send_response(data["response_status_code"])
                self.flush_headers()
                self.wfile.write(data["response_headers"].encode("latin1", "strict"))
                self.end_headers()
                self.wfile.write(data["response_body"].encode("utf-8"))
        except:
            self.send_response(500)
            self.end_headers()

        threading.Thread(target=server.shutdown).start()


server = http.server.HTTPServer(('', 8080), Handler)
server_thread = threading.Thread(target=server.serve_forever)
server_thread.start()

for test in tests:
    if test["request_method"] == "GET":
        table = "http_get"
    elif test["request_method"] == "POST":
        table = "http_post"
    request_url = test["request_url"]
    sql = f".mode json\nselect request_method, request_url, request_headers, request_body, * from {table}('{request_url}')"
    cp = subprocess.run(["./build/tests/Debug/sqlite3.exe", ":memory:"], input=sql, text=True, capture_output=True)
    j = json.loads(cp.stdout)
    print(j)
    assert j[0]["response_status_code"] == test["response_status_code"]
    assert j[0]["response_body"] == test["response_body"]
    assert j[0]["response_headers"] == "Date: Mon, 05 Sep 2022 19:26:53 GMT\r\nServer: testclient/1.0\r\n" + test["response_headers"] + "\r\n"

server_thread.join()
