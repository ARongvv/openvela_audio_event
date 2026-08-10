#!/usr/bin/env python3
"""LAN HTTP receiver and lightweight dashboard for audio_event alerts.

The ESP32-S3 sends POST requests to /api/v1/audio-events. Open / in a
browser on the same LAN to inspect the most recent accepted events.
"""

import argparse
import json
import socket
import struct
import threading
import time
from collections import Counter, OrderedDict, deque
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse


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
DEFAULT_PCM_PORT = 5004
PCM_HEADER = struct.Struct("!IHIQHH")
PCM_MAGIC = 0x41455043
PCM_VERSION = 1
PCM_SAMPLE_RATE = 16000
PCM_ENVELOPE_BIN_SAMPLES = 32


DASHBOARD_HTML = """<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Audio Event Dashboard</title>
<style>
  :root { color-scheme: dark; font-family: Inter, ui-sans-serif, system-ui, sans-serif; }
  body { margin: 0; min-height: 100vh; background: radial-gradient(circle at 20% 0%, #17283d 0, #10151d 42%); color: #e8edf5; }
  main { max-width: 1120px; margin: auto; padding: 24px; }
  h1 { margin: 0 0 6px; font-size: 1.55rem; }
  .hint { color: #9cacbe; margin: 0 0 20px; }
  .cards { display: grid; grid-template-columns: repeat(4, minmax(130px, 1fr)); gap: 12px; }
  .card, .panel { background: rgba(25, 33, 45, .94); border: 1px solid #2b394a; border-radius: 12px; padding: 14px; box-shadow: 0 12px 28px rgba(0,0,0,.12); }
  .card span { color: #9cacbe; display: block; font-size: .82rem; }
  .card strong { display: block; font-size: 1.4rem; overflow-wrap: anywhere; margin-top: 4px; }
  .panel { margin-top: 16px; overflow-x: auto; }
  .pcm-head { display: flex; align-items: baseline; gap: 12px; flex-wrap: wrap; }
  .pcm-head h2 { margin: 4px 0; }
  #pcm-status { color: #70d6ff; font-size: .9rem; }
  #pcm-meta { color: #9cacbe; font-size: .78rem; }
  canvas { width: 100%; height: 280px; display: block; background: #0d1520; border-radius: 8px; }
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
  <p class="hint">ESP32-S3 局域网告警接收端 · 告警每秒刷新 / PCM 10 Hz 刷新</p>
  <section class="cards">
    <div class="card"><span>接收事件</span><strong id="total">0</strong></div>
    <div class="card"><span>最近事件</span><strong id="latest">—</strong></div>
    <div class="card"><span>最近设备</span><strong id="device">—</strong></div>
    <div class="card"><span>类别统计</span><strong id="counts">—</strong></div>
  </section>
  <section class="panel">
    <div class="pcm-head"><h2>实时 PCM 波形</h2><small id="pcm-status">等待 UDP 数据</small><small id="pcm-meta"></small></div>
    <canvas id="waveform" width="1040" height="180"></canvas>
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
const PCM_WINDOW_MS = 5000;
const PCM_POLL_MS = 100;
const PCM_DISPLAY_RANGE = 16000;
let pcmPoints = [];
let pcmCursor = null;
let pcmStreamId = null;
let pcmPolling = false;

function cell(row, text, className = '') {
  const td = document.createElement('td');
  td.textContent = text;
  td.className = className;
  row.appendChild(td);
}

function resizeWaveform() {
  const canvas = byId('waveform');
  const ratio = Math.min(2, window.devicePixelRatio || 1);
  const width = Math.max(1, Math.floor(canvas.clientWidth * ratio));
  const height = Math.max(1, Math.floor(canvas.clientHeight * ratio));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  return {canvas, ratio, width, height};
}

function drawWaveform() {
  const {canvas, ratio, width, height} = resizeWaveform();
  const ctx = canvas.getContext('2d');
  const cssWidth = width / ratio;
  const cssHeight = height / ratio;
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  ctx.clearRect(0, 0, cssWidth, cssHeight);

  const end = pcmPoints.length ? pcmPoints[pcmPoints.length - 1].timestamp_ms : 0;
  const start = end - PCM_WINDOW_MS;
  const points = pcmPoints.filter(point => point.timestamp_ms >= start);
  const center = cssHeight / 2;
  const amplitude = center - 25;
  const xFor = timestamp => (timestamp - start) * cssWidth / PCM_WINDOW_MS;
  const yFor = sample => {
    const clipped = Math.max(-PCM_DISPLAY_RANGE,
                             Math.min(PCM_DISPLAY_RANGE, sample));
    return center - clipped * amplitude / PCM_DISPLAY_RANGE;
  };

  const background = ctx.createLinearGradient(0, 0, 0, cssHeight);
  background.addColorStop(0, '#0d1825');
  background.addColorStop(1, '#0b111a');
  ctx.fillStyle = background;
  ctx.fillRect(0, 0, cssWidth, cssHeight);

  ctx.lineWidth = 1;
  ctx.strokeStyle = 'rgba(106, 139, 175, .18)';
  ctx.setLineDash([3, 5]);
  for (let i = 0; i <= 4; i++) {
    const y = i * cssHeight / 4;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(cssWidth, y); ctx.stroke();
  }
  for (let i = 0; i <= 5; i++) {
    const x = i * cssWidth / 5;
    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, cssHeight); ctx.stroke();
  }
  ctx.setLineDash([]);
  ctx.strokeStyle = 'rgba(167, 190, 216, .35)';
  ctx.beginPath(); ctx.moveTo(0, center); ctx.lineTo(cssWidth, center); ctx.stroke();

  if (points.length) {
    const fill = ctx.createLinearGradient(0, 12, 0, cssHeight - 12);
    fill.addColorStop(0, 'rgba(91, 218, 255, .48)');
    fill.addColorStop(.5, 'rgba(74, 166, 234, .13)');
    fill.addColorStop(1, 'rgba(91, 218, 255, .48)');
    ctx.beginPath();
    points.forEach((point, index) => {
      const x = xFor(point.timestamp_ms);
      const y = yFor(point.max);
      if (index) ctx.lineTo(x, y); else ctx.moveTo(x, y);
    });
    for (let index = points.length - 1; index >= 0; index--) {
      const point = points[index];
      ctx.lineTo(xFor(point.timestamp_ms), yFor(point.min));
    }
    ctx.closePath();
    ctx.fillStyle = fill;
    ctx.fill();
    ctx.strokeStyle = '#62d8ff';
    ctx.lineWidth = 1.1;
    ctx.beginPath();
    points.forEach((point, index) => {
      const x = xFor(point.timestamp_ms);
      if (index) ctx.lineTo(x, yFor(point.max)); else ctx.moveTo(x, yFor(point.max));
    });
    ctx.stroke();
    ctx.beginPath();
    points.forEach((point, index) => {
      const x = xFor(point.timestamp_ms);
      if (index) ctx.lineTo(x, yFor(point.min)); else ctx.moveTo(x, yFor(point.min));
    });
    ctx.stroke();
  }

  ctx.fillStyle = '#90a4bc';
  ctx.font = '12px system-ui';
  ctx.fillText('−5 s', 10, cssHeight - 9);
  ctx.fillText('现在', cssWidth - 34, cssHeight - 9);
  ctx.fillText(`固定量程 ±${PCM_DISPLAY_RANGE}`, 10, 17);
  requestAnimationFrame(drawWaveform);
}

async function refreshEvents() {
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

async function pollPcm() {
  if (pcmPolling) return;
  pcmPolling = true;
  try {
    const suffix = pcmCursor === null ? '' : `?since=${encodeURIComponent(pcmCursor)}`;
    const response = await fetch('/api/v1/audio-window' + suffix, {cache: 'no-store'});
    if (!response.ok) throw new Error('HTTP ' + response.status);
    const data = await response.json();
    if (pcmStreamId !== null && data.stream_id !== pcmStreamId) {
      pcmPoints = [];
      pcmCursor = null;
    }
    pcmStreamId = data.stream_id;
    if (data.points.length) {
      pcmPoints.push(...data.points);
      pcmCursor = data.points[data.points.length - 1].timestamp_ms;
      const cutoff = pcmCursor - PCM_WINDOW_MS;
      pcmPoints = pcmPoints.filter(point => point.timestamp_ms >= cutoff);
    }
    setText('pcm-status', `接收 ${data.received_packets} 包 · 丢失 ${data.lost_packets} 包`);
    setText('pcm-meta', `10 Hz 刷新 · ${pcmPoints.length} 个包络点`);
  } catch (error) {
    setText('pcm-status', '波形连接失败：' + error.message);
  } finally {
    pcmPolling = false;
  }
}

refreshEvents();
pollPcm();
setInterval(refreshEvents, 1000);
setInterval(pollPcm, PCM_POLL_MS);
window.addEventListener('resize', resizeWaveform);
requestAnimationFrame(drawWaveform);
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


class PcmStore:
    """Bounded PCM envelope for visualization; raw PCM is not retained."""

    def __init__(self, max_points):
        self._points = deque(maxlen=max_points)
        self._received_packets = 0
        self._lost_packets = 0
        self._invalid_packets = 0
        self._last_sequence = None
        self._stream_id = 1
        self._lock = threading.Lock()

    def add(self, sequence, timestamp_ms, samples):
        with self._lock:
            if self._last_sequence is not None and sequence <= self._last_sequence:
                # A device reboot starts its monotonically increasing packet
                # sequence again. Do not join two unrelated timelines.
                self._stream_id += 1
                self._last_sequence = None
                self._points.clear()
            if self._last_sequence is not None and sequence > self._last_sequence + 1:
                self._lost_packets += sequence - self._last_sequence - 1
            self._last_sequence = sequence
            self._received_packets += 1

            # One UDP packet spans 32 ms. Preserve a 2 ms min/max envelope
            # rather than a single bar, which makes the browser waveform
            # continuous without retaining raw voice data.
            sample_count = len(samples)
            start_ms = timestamp_ms - sample_count * 1000 / PCM_SAMPLE_RATE
            for offset in range(0, sample_count, PCM_ENVELOPE_BIN_SAMPLES):
                chunk = samples[offset:offset + PCM_ENVELOPE_BIN_SAMPLES]
                point_time = start_ms + offset * 1000 / PCM_SAMPLE_RATE
                self._points.append((round(point_time, 3), min(chunk), max(chunk)))

    def note_invalid(self):
        with self._lock:
            self._invalid_packets += 1

    def snapshot(self, since_ms=None):
        with self._lock:
            if since_ms is None:
                points = self._points
            else:
                points = (point for point in self._points if point[0] > since_ms)
            return {"received_packets": self._received_packets,
                    "lost_packets": self._lost_packets,
                    "invalid_packets": self._invalid_packets,
                    "stream_id": self._stream_id,
                    "points": [{"timestamp_ms": timestamp_ms,
                                "min": min_sample, "max": max_sample}
                               for timestamp_ms, min_sample, max_sample in points]}


def pcm_receiver_loop(sock, store):
    while True:
        packet, _ = sock.recvfrom(1400)
        if len(packet) < PCM_HEADER.size:
            store.note_invalid()
            continue
        magic, version, sequence, timestamp_ms, sample_count, _ = PCM_HEADER.unpack_from(packet)
        payload_size = sample_count * 2
        if (magic != PCM_MAGIC or version != PCM_VERSION or sample_count == 0 or
                sample_count > 512 or len(packet) != PCM_HEADER.size + payload_size):
            store.note_invalid()
            continue
        samples = struct.unpack_from("<%dh" % sample_count, packet, PCM_HEADER.size)
        store.add(sequence, timestamp_ms, samples)


class AudioEventHandler(BaseHTTPRequestHandler):
    store = None
    pcm_store = None

    def _send_json(self, status, payload):
        encoded = (json.dumps(payload, ensure_ascii=False) + "\n").encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def do_GET(self):
        request = urlparse(self.path)

        if request.path == "/":
            encoded = DASHBOARD_HTML.encode("utf-8")
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(encoded)))
            self.end_headers()
            self.wfile.write(encoded)
        elif request.path == "/api/v1/events":
            self._send_json(HTTPStatus.OK, self.store.snapshot())
        elif request.path == "/api/v1/audio-window":
            query = parse_qs(request.query)
            try:
                since_values = query.get("since", [])
                since_ms = float(since_values[0]) if since_values else None
            except ValueError:
                self.send_error(HTTPStatus.BAD_REQUEST, "invalid since")
                return
            self._send_json(HTTPStatus.OK, self.pcm_store.snapshot(since_ms))
        elif request.path == "/healthz":
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
    parser.add_argument("--pcm-port", type=int, default=DEFAULT_PCM_PORT)
    args = parser.parse_args()
    if args.max_events <= 0 or args.max_seen <= 0:
        parser.error("--max-events and --max-seen must be positive")

    AudioEventHandler.store = EventStore(args.max_events, args.max_seen)
    # 16 envelope points per 32 ms packet retain over 8 seconds of display
    # history while using substantially less memory than raw PCM.
    AudioEventHandler.pcm_store = PcmStore(max_points=4096)
    pcm_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    pcm_socket.bind((args.host, args.pcm_port))
    threading.Thread(target=pcm_receiver_loop,
                     args=(pcm_socket, AudioEventHandler.pcm_store),
                     daemon=True).start()
    server = ThreadingHTTPServer((args.host, args.port), AudioEventHandler)
    print(f"dashboard: http://{args.host}:{args.port}/")
    print(f"receiver:  http://{args.host}:{args.port}/api/v1/audio-events")
    print(f"pcm UDP:   {args.host}:{args.pcm_port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
