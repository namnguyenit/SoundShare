"""Control/backend integration only: never starts audio or opens Bluetooth receivers."""
import json
import pathlib
import subprocess
import sys
import tempfile
import time
import uuid
import concurrent.futures

worker, ctl = sys.argv[1:3]

def check(value, message):
    if not value:
        raise AssertionError(message)

def payload(method, params=None, ident=1):
    return {"protocol": 1, "id": ident, "method": method, "params": params or {}}

process = subprocess.Popen([worker, "--stdio"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True, encoding="utf-8")

def request(value):
    line = value if isinstance(value, str) else json.dumps(value, ensure_ascii=False)
    process.stdin.write(line + "\n")
    process.stdin.flush()
    reply = process.stdout.readline()
    check(bool(reply), "worker terminated unexpectedly")
    return json.loads(reply)

try:
    hello = request(payload("hello"))["result"]
    check(hello["protocol"] == 1 and isinstance(hello["instance_id"], str), "hello identity")
    check(request(payload("hello"))["result"]["instance_id"] == hello["instance_id"], "worker identity unstable")
    check(not request("{")["ok"], "invalid JSON accepted")
    check(not request({"protocol": 2, "method": "status"})["ok"], "version accepted")
    check(not request(payload("missing"))["ok"], "unknown method accepted")
    check(not request({"protocol": 1, "method": "status", "params": []})["ok"], "array params accepted")
    check(not request({"protocol": 1, "method": "status", "id": {}})["ok"], "object id accepted")
    check(not request("x" * 65537)["ok"], "oversized request accepted")
    check(not request("[" * 1000 + "0" + "]" * 1000)["ok"], "deeply nested JSON accepted")
    config = {"version": 1, "capture": {"id": "bridge-recording", "mode": "capture", "bridge_render_id": "bridge-playback"},
              "buffer_ms": 120, "outputs": [{"id": "physical-output", "gain": 0.5, "delay_ms": 50}]}
    check(request(payload("config.validate", config))["ok"], "valid config rejected")
    for bad in [-1, 0, 501, 2**64]:
        variant = dict(config, buffer_ms=bad)
        check(not request(payload("config.validate", variant))["ok"], "invalid buffer accepted")
    for outputs in [[{"id": "bridge-playback"}], [{"id": "same"}, {"id": "same"}], [{"id": "x", "gain": 2}],
                    [{"id": "x", "delay_ms": -1}], [{"id": "x", "surprise": True}], []]:
        check(not request(payload("config.validate", dict(config, outputs=outputs)))["ok"], "bad output config accepted")
    check(not request(payload("receiver.open", {"id": "x", "auto_reconnect": 1}))["ok"], "bad reconnect flag accepted")
    check(not request(payload("output.configure", {"id": "x"}))["ok"], "configure inactive session")
    check(not request(payload("master.configure", {"gain": 2}))["ok"], "invalid master gain")
    check(request(payload("session.stop"))["ok"], "idempotent stop")
    check(request(payload("output.remove", {"id": "already-absent"}))["ok"], "idempotent remove")
    with tempfile.TemporaryDirectory(prefix="soundshare-test-") as directory:
        path = pathlib.Path(directory) / "cấu-hình.json"
        saved = request(payload("config.save", {"path": str(path), "config": config}))
        check(saved["ok"], "config save")
        check(json.loads(path.read_text(encoding="utf-8"))["outputs"][0]["gain"] == 0.5, "saved data")
        check(request(payload("config.load", {"path": str(path)}))["ok"], "config load")
        current = request(payload("config.current"))
        check(current["ok"] and not current["result"]["active"], "current staged config state")
        check(current["result"]["config"]["capture"]["id"] == "bridge-recording", "current staged capture")
        check(current["result"]["config"]["outputs"][0]["delay_ms"] == 50, "current staged delay")
        staged_copy = pathlib.Path(directory) / "staged-copy.json"
        check(request(payload("config.save", {"path": str(staged_copy)}))["ok"], "save loaded config without starting")
        check(json.loads(staged_copy.read_text(encoding="utf-8"))["capture"]["id"] == "bridge-recording", "saving staged config used empty engine configuration")
        check(not list(path.parent.glob("*.tmp.*")), "temporary save file leaked")
        invalid = pathlib.Path(directory) / "invalid.json"
        invalid.write_text("{}", encoding="utf-8")
        check(not request(payload("config.load", {"path": str(invalid)}))["ok"], "invalid config loaded")
    inventory = request(payload("inventory"))
    check(inventory["ok"] and isinstance(inventory["result"]["endpoints"], list), "real Windows endpoint inventory")
    check(not request(payload("session.start", {"config": config}))["ok"], "nonexistent input started")
    check(not request(payload("status"))["result"]["engine"]["active"], "failed start mutated active state")
    check(request(payload("shutdown"))["ok"], "shutdown")
    process.wait(timeout=15)
    check(process.returncode == 0, "stdio worker exit")
finally:
    if process.poll() is None:
        process.kill()
        process.wait()

pipe_name = "SoundShare.Test." + uuid.uuid4().hex
server = subprocess.Popen([worker, "--pipe", pipe_name], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
try:
    reply = None
    for _ in range(30):
        reply = subprocess.run([ctl, "--pipe", pipe_name, "hello"], capture_output=True, text=True, encoding="utf-8", timeout=15)
        if reply.returncode == 0:
            break
        time.sleep(0.1)
    check(reply.returncode == 0, "named pipe hello: " + reply.stderr)
    check(json.loads(reply.stdout)["result"]["protocol"] == 1, "named pipe response")
    def concurrent_status(_):
        response = subprocess.run([ctl, "--pipe", pipe_name, "status"], capture_output=True,
                                  text=True, encoding="utf-8", timeout=15)
        check(response.returncode == 0 and json.loads(response.stdout)["ok"], "concurrent local clients failed: " + response.stderr)
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as clients:
        list(clients.map(concurrent_status, range(32)))
    duplicate = subprocess.run([worker, "--pipe", pipe_name], capture_output=True, timeout=15)
    check(duplicate.returncode != 0, "duplicate server claimed pipe")
    status = subprocess.run([ctl, "--pipe", pipe_name, "status"], capture_output=True, text=True, encoding="utf-8", timeout=15)
    check(status.returncode == 0, "named pipe status")
    shutdown = subprocess.run([ctl, "--pipe", pipe_name, "shutdown"], capture_output=True, text=True, encoding="utf-8", timeout=15)
    check(shutdown.returncode == 0, "named pipe shutdown: " + shutdown.stderr)
    server.wait(timeout=15)
    check(server.returncode == 0, "named pipe worker exit")
finally:
    if server.poll() is None:
        server.kill()
        server.wait()
print("PASS backend validation, Unicode config persistence, real inventory and named-pipe lifecycle")
