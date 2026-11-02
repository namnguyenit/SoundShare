"""Opt-in WASAPI initialization/lifecycle checks. All SoundShare outputs remain muted.

No receiver is opened, no default endpoint is changed, and no PCM is written to disk.
"""
import argparse
import json
import pathlib
import subprocess
import time
import ctypes
from ctypes import wintypes

parser = argparse.ArgumentParser()
parser.add_argument("worker")
parser.add_argument("--run-muted-audio", action="store_true")
parser.add_argument("--seconds", type=int, default=15)
parser.add_argument("--report")
parser.add_argument("--restart-cycles", type=int, default=3)
args = parser.parse_args()
if args.seconds < 1 or args.seconds > 120:
    parser.error("--seconds must be 1..120")
if not 1 <= args.restart_cycles <= 200:
    parser.error("--restart-cycles must be 1..200")
process = subprocess.Popen([args.worker, "--stdio"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True, encoding="utf-8")
report = {"audio_recorded": False, "defaults_changed": False, "receivers_opened": False}

def cpu_seconds():
    get_times = ctypes.windll.kernel32.GetProcessTimes
    get_times.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    stamps = [wintypes.FILETIME() for _ in range(4)]
    if not get_times(wintypes.HANDLE(int(process._handle)), *[ctypes.byref(value) for value in stamps]):
        raise ctypes.WinError()
    return sum((value.dwHighDateTime << 32) | value.dwLowDateTime for value in stamps[2:]) / 1e7

def request(method, params=None):
    process.stdin.write(json.dumps({"protocol": 1, "id": method, "method": method, "params": params or {}}) + "\n")
    process.stdin.flush()
    value = json.loads(process.stdout.readline())
    if not value["ok"]:
        raise RuntimeError(json.dumps(value, ensure_ascii=False))
    return value["result"]

try:
    devices = request("inventory")["endpoints"]
    renders = [item for item in devices if item["active"] and item["direction"] == "render"]
    report["render_endpoints"] = renders
    report["receiver_inventory"] = request("receiver.list")
    report["probes"] = []
    for device in renders:
        for mode in ["render", "loopback"]:
            probe = request("audio.probe", {"id": device["id"], "mode": mode})
            report["probes"].append({"name": device["name"], "mode": mode, **probe})
    if args.run_muted_audio:
        if len(renders) < 2:
            raise RuntimeError("need a loopback endpoint and a distinct render endpoint")
        source = next((item for item in renders if item["default_multimedia"]), renders[0])
        outputs = [item for item in renders if item["id"] != source["id"]][:2]
        config = {"version": 1, "capture": {"id": source["id"], "mode": "loopback"}, "buffer_ms": 120,
                  "master_gain": 0, "outputs": [{"id": item["id"], "gain": 0, "muted": True} for item in outputs]}
        request("session.start", {"config": config})
        states = []
        measured_at = time.monotonic()
        initial_cpu = cpu_seconds()
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            status = request("status")["engine"]
            if status["capture"]["state"] == "failed" or any(item["state"] == "failed" for item in status["outputs"]):
                raise RuntimeError(json.dumps(status, ensure_ascii=False))
            if any(item["peak"] != 0 for item in status["outputs"]):
                raise RuntimeError("muted output contains nonzero PCM")
            states.append(status)
            time.sleep(0.1)
        if not any(item["all_streams_running"] for item in states):
            raise RuntimeError("streams never reached running state")
        measured_seconds = time.monotonic() - measured_at
        report["silent_session"] = {"seconds": measured_seconds, "output_count": len(outputs), "last_status": states[-1],
                                   "worker_cpu_percent_one_core": (cpu_seconds() - initial_cpu) * 100 / measured_seconds}
        # Exercise atomic configuration, removal and slot reuse without changing audible output.
        request("output.configure", {"id": outputs[0]["id"], "gain": 0, "muted": True, "delay_ms": 50})
        request("master.configure", {"gain": 0})
        if len(outputs) > 1:
            request("output.remove", {"id": outputs[1]["id"]})
            request("output.add", {"id": outputs[1]["id"], "gain": 0, "muted": True})
            time.sleep(0.3)
        request("session.stop")
        if request("status")["engine"]["active"]:
            raise RuntimeError("session did not stop")
        for _ in range(args.restart_cycles):
            request("session.start", {"config": config})
            time.sleep(0.15)
            request("session.stop")
        report["restart_cycles"] = args.restart_cycles
    request("shutdown")
    process.wait(timeout=15)
    report["passed"] = process.returncode == 0
    if not report["passed"]:
        raise RuntimeError("worker failed during shutdown")
finally:
    if process.poll() is None:
        process.kill()
        process.wait()
if args.report:
    pathlib.Path(args.report).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(args.report).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps({"passed": report["passed"], "probe_count": len(report["probes"]),
                  "receiver_names": [item["name"] for item in report["receiver_inventory"]["devices"]],
                  "silent_session": report.get("silent_session")}, ensure_ascii=False))
