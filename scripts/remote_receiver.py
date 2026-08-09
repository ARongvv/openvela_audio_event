#!/usr/bin/env python3
"""LAN HTTP receiver and lightweight dashboard for audio_event alerts.

The ESP32-S3 sends POST requests to /api/v1/audio-events. Open / in a
browser on the same LAN to inspect the most recent accepted events.
"""

import argparse
import json
import threading
import time
from collections import Counter, OrderedDict, deque
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


REQUIRED_FIELDS = {
    "schema_version",
    "event_id",
    "device_id",
    "event",
    "confidence_permille",
    "monotonic_ms",
}

DEFAULT_MAX_EVENTS = 200
DEFAULT_MAX_SEEN = 1024


DASHBOARD_HTML = """<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Audio Event Dashboard</title>
<style>
  :root { color-scheme: dark; font-family: system-ui, sans-serif; }
  body { margin: 0; background: #10151d; color: #e8edf5; }
  main { max-width: 1120px; margin: auto; padding: 24px; }
  h1 { margin: 0 0 6px; font-size: 1.55rem; }
  .hint { color: #9cacbe; margin: 0 0 20px; }
  .cards { display: grid; grid-template-columns: repeat(4, minmax(130px, 1fr)); gap: 12px; }
  .card, .panel { background: #19212d; border: 1px solid #2b394a; border-radius: 10px; padding: 14px; }
  .card span { color: #9cacbe; display: block; font-size: .82rem; }
  .card strong { display: block; font-size: 1.4rem; overflow-wrap: anywhere; margin-top: 4px; }
  .panel { margin-top: 16px; overflow-x: auto; }
  table { border-collapse: collapse; width: 100%; min-width: 720px; }
  th, td { border-bottom: 1px solid #2b394a; padding: 10px 8px; text-align: left; }
  th { color: #9cacbe; font-size: .8rem; }
  .event { font-weight: 700; color: #70d6ff; }
  .confidence { font-variant-numeric: tabular-nums; }
  .empty { color: #9cacbe; text-align: center; padding: 28px; }
  #status { float: right; color: #8bd3a8; font-size: .85rem; }
  @media (max-width: 640px) { .cards { grid-template-columns: repeat(2, 1fr); } }
</style>
</head>
<body>
<main>
  <span id="status">连接中…</span>
  <h1>Audio Event Dashboard</h1>
  <p class="hint">ESP32-S3 局域网告警接收端 · 每秒自动刷新</p>
  <section class="cards">
    <div class="card"><span>接收事件</span><strong id="total">0</strong></div>
    <div class="card"><span>最近事件</span><strong id="latest">—</strong></div>
    <div class="card"><span>最近设备</span><strong id="device">—</strong></div>
    <div class="card"><span>类别统计</span><strong id="counts">—</strong></div>
  </section>
  <section class="panel">
    <table>
      <thead><tr><th>接收时间</th><th>设备</th><th>事件</th><th>置信度</th><th>音频时间</th><th>事件 ID</th></tr></thead>
      <tbody id="events"><tr><td class="empty" colspan="6">尚未收到设备告警</td></tr></tbody>
    </table>
  </section>
</main>
<script>
const byId = id => document.getElementById(id);
const setText = (id, text) => { byId(id).textContent = text; };
const formatTime = ms => new Date(ms).toLocaleString();

function cell(row, text, className = '') {
  const td = document.createElement('td');
  td.textContent = text;
  td.className = className;
  row.appendChild(td);
}

async function refresh() {
  try {
    const response = await fetch('/api/v1/events', {cache: 'no-store'});
    if (!response.ok) throw new Error('HTTP ' + response.status);
    const data = await response.json();
    setText('total', data.total_received);
    const latest = data.events[0];
    setText('latest', latest ? latest.event : '—');
    setText('device', latest ? latest.device_id : '—');
    const counts = Object.entries(data.event_counts)
      .map(([name, count]) => `${name}: ${count}`).join(' · ');
    setText('counts', counts || '—');
    const body = byId('events');
    body.replaceChildren();
    if (!data.events.length) {
      const row = document.createElement('tr');
      const td = document.createElement('td');
      td.colSpan = 6; td.className = 'empty'; td.textContent = '尚未收到设备告警';
      row.appendChild(td); body.appendChild(row); return;
    }
    for (const event of data.events) {
      const row = document.createElement('tr');
      cell(row, formatTime(event.received_unix_ms));
      cell(row, event.device_id);
      cell(row, event.event, 'event');
      cell(row, `${event.confidence_permille} / 1000`, 'confidence');
      cell(row, `${event.monotonic_ms} ms`);
      cell(row, event.event_id);
      body.appendChild(row);
    }
    setText('status', '在线 · ' + new Date().toLocaleTimeString());
  } catch (error) {
    setText('status', '连接失败：' + error.message);
  }
}
refresh();
setInterval(refresh, 1000);
</script>
</body>
</html>
"""


class EventStore:
    """Thread-safe bounded event history and duplicate tracker."""

    def __init__(self, max_events, max_seen):
        self._events = deque(maxlen=max_events)
        self._seen_event_ids = OrderedDict()
        self._max_seen = max_seen
        self._total_received = 0
        self._lock = threading.Lock()

    def add(self, event):
        event_id = event["event_id"]
        record = dict(event)
        record["received_unix_ms"] = int(time.time() * 1000)
        with self._lock:
            duplicate = event_id in self._seen_event_ids
            self._seen_event_ids[event_id] = None
            self._seen_event_ids.move_to_end(event_id)
            while len(self._seen_event_ids) > self._max_seen:
                self._seen_event_ids.popitem(last=False)
            if not duplicate:
                self._events.appendleft(record)
                self._total_received += 1
        return duplicate, record

    def snapshot(self):
        with self._lock:
            events = list(self._events)
            return {
                "total_received": self._total_received,
                "event_counts": dict(Counter(event["event"] for event in events)),
                "events": events,
            }


class AudioEventHandler(BaseHTTPRequestHandler):
    store = None

    def _send_json(self, status, payload):
        encoded = (json.dumps(payload, ensure_ascii=False) + "\n").encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def do_GET(self):
        if self.path == "/":
            encoded = DASHBOARD_HTML.encode("utf-8")
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(encoded)))
            self.end_headers()
            self.wfile.write(encoded)
        elif self.path == "/api/v1/events":
            self._send_json(HTTPStatus.OK, self.store.snapshot())
        elif self.path == "/healthz":
            self._send_json(HTTPStatus.OK, {"status": "ok"})
        else:
            self.send_error(HTTPStatus.NOT_FOUND, "unknown endpoint")

    def do_POST(self):
        if self.path != "/api/v1/audio-events":
            self.send_error(HTTPStatus.NOT_FOUND, "unknown endpoint")
            return

        try:
            content_length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self.send_error(HTTPStatus.BAD_REQUEST, "invalid Content-Length")
            return

        if content_length <= 0 or content_length > 4096:
            self.send_error(HTTPStatus.REQUEST_ENTITY_TOO_LARGE,
                            "payload size must be 1..4096 bytes")
            return

        try:
            event = json.loads(self.rfile.read(content_length))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self.send_error(HTTPStatus.BAD_REQUEST, "invalid JSON")
            return

        if not isinstance(event, dict):
            self.send_error(HTTPStatus.BAD_REQUEST, "JSON payload must be an object")
            return

        missing = REQUIRED_FIELDS.difference(event)
        if missing:
            self.send_error(HTTPStatus.BAD_REQUEST,
                            "missing fields: " + ", ".join(sorted(missing)))
            return

        duplicate, record = self.store.add(event)
        print(json.dumps({"duplicate": duplicate, "event": record},
                         ensure_ascii=False), flush=True)
        self._send_json(HTTPStatus.OK, {"accepted": True, "duplicate": duplicate})

    def log_message(self, fmt, *args):
        print("[http] " + fmt % args, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--max-events", type=int, default=DEFAULT_MAX_EVENTS)
    parser.add_argument("--max-seen", type=int, default=DEFAULT_MAX_SEEN)
    args = parser.parse_args()
    if args.max_events <= 0 or args.max_seen <= 0:
        parser.error("--max-events and --max-seen must be positive")

    AudioEventHandler.store = EventStore(args.max_events, args.max_seen)
    server = ThreadingHTTPServer((args.host, args.port), AudioEventHandler)
    print(f"dashboard: http://{args.host}:{args.port}/")
    print(f"receiver:  http://{args.host}:{args.port}/api/v1/audio-events")
    server.serve_forever()


if __name__ == "__main__":
    main()
