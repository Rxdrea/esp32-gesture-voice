"""仅用于电脑浏览器检查布局；不会连接硬件，不产生模拟数据。"""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
ROOT = Path(__file__).resolve().parents[1] / 'web'
class Preview(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)
    def do_GET(self):
        if self.path.startswith('/api/'):
            data=json.dumps({'ok':False,'error':'当前为布局预览，尚未连接ESP32，无传感器数据'},ensure_ascii=False).encode()
            self.send_response(503);self.send_header('Content-Type','application/json; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        else:
            super().do_GET()
    def do_POST(self):
        self.send_error(503, 'Preview only, no hardware control')
    def log_message(self, *args):
        pass
if __name__ == '__main__':
    server=ThreadingHTTPServer(('127.0.0.1',8766), Preview)
    print('本地布局预览 http://127.0.0.1:8766 （无硬件数据，不模拟）',flush=True)
    server.serve_forever()