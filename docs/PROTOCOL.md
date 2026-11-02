**SoundShare backend protocol v1**

Transport: UTF-8 JSON objects separated by LF. Worker supports `--stdio` and a local, current-user/session named pipe. Requests are limited to 64 KiB and 32 nesting levels. Responses over named pipe are limited to 1 MiB. No PCM crosses this control channel.

Request:

```json
{"protocol":1,"id":"request-123","method":"status","params":{}}
```

Success: `{"protocol":1,"id":"request-123","ok":true,"result":{...}}`.
Failure: `{"protocol":1,"id":"request-123","ok":false,"error":{"code":"...","message":"...","hresult":"0x..."}}`.

IDs are strings, integers or null. Protocol must be integer 1. Parameters must be objects. Unknown fields are rejected to catch configuration mistakes. `accepted` means the command was accepted; asynchronous WASAPI and Bluetooth opening/recovery must be checked through `status`.

| Method | Parameters | Behavior |
| --- | --- | --- |
| `hello` | `{}` | Version, capabilities, pipe name and a stable `instance_id` for this worker lifetime |
| `inventory` | `{}` | All Windows endpoint IDs, names, direction, state, default roles, native mix format |
| `receiver.list` | `{}` | A2DP receiver candidates and held connections; discovery does not open them |
| `receiver.open` | `id`, optional `auto_reconnect` boolean | Queue opening on the receiver thread; returns `accepted` and connections (`opening` before completion); opt-in reconnection is limited to five additional attempts |
| `receiver.close` | `id` | Close/release one receiver; missing entry is an idempotent success |
| `status` | `{}` | Engine and source states/counters; does not verify acoustic playback |
| `config.current` | `{}` | `{active, config}` for the active or staged session; does not start hardware |
| `config.validate` | Full config object | Validate types, bounds, duplicates and feedback guard; does not open hardware |
| `config.save` | `path`, optional `config` | Save provided, active, or staged config through temporary file + replace |
| `config.load` | `path` | Validate and stage config; never starts playback; rejected during an active session |
| `session.start` | Optional `config` | Start provided or staged config; rejects replacing an active session |
| `session.stop` | `{}` | Close receivers, stop capture and stop all outputs; idempotent |
| `output.add` | Full output object | Join one output without restarting healthy branches |
| `output.remove` | `id` | Fade/stop one branch; missing branch is an idempotent success; does not restart the others |
| `output.configure` | Full output object | Replace branch gain/mute/delay settings; omitted settings use defaults, not previous values |
| `master.configure` | `gain` number 0..1 | Apply ramped master gain to every branch |
| `audio.probe` | `id`, `mode`: `render`, `capture` or `loopback` | Initialize shared WASAPI without starting; privacy/device rules still apply to capture endpoints |
| `diagnostics.export` | `path` | Snapshot config, endpoints, counters and recent errors; no audio recording |
| `shutdown` | `{}` | Close receivers and stop engine; return response then terminate transport |

Config:

```json
{
  "version": 1,
  "capture": {
    "id": "recording-endpoint-id",
    "mode": "capture",
    "bridge_render_id": "matching-virtual-playback-endpoint-id"
  },
  "buffer_ms": 120,
  "master_gain": 0.5,
  "outputs": [
    {"id":"physical-render-id","gain":1,"muted":false,"delay_ms":0}
  ]
}
```

`capture` mode reads a recording endpoint, including a virtual cable recording endpoint. `loopback` mode reads a render endpoint. Supply the associated `bridge_render_id` when using a virtual cable so that selecting its playback side as an output is rejected. The backend cannot discover every external mixer/driver's arbitrary internal feedback graph.

`buffer_ms` is application queue reserve (20..500), not total end-to-end latency or WASAPI hardware buffer size. `delay_ms` is additional branch delay (0..1000). Each queue has capacity for two seconds of stereo PCM; normal resampling uses a small accounted internal reservoir. Up to eight output branches can be configured; this is a software limit, not a claim about eight Bluetooth headsets on one radio.

All streams request shared 48 kHz float32 stereo with Windows format conversion. Driver/device rejection is reported rather than silently switching to exclusive mode. Clock sampling is kept on a separate thread; the render path uses atomic clock estimates and bounded queue-based drift correction. Format changes/invalidation reopen the affected stream, with finite backoff.

During loopback idle periods Windows may provide no capture packets. The capture worker publishes timed silence after an idle grace period. These frames are reported separately as `idle_silence_frames`; they are not evidence of healthy Bluetooth transport. Sample discontinuities, underruns and dropped queue frames are likewise not RF packet-loss counts.

`state=running` means the stream has started, not that the remote device is audible. `all_streams_running` is a stream-lifecycle flag. Capability fields `per_source_pcm`, `playing_verified`, `simultaneous_sources_verified` and `acoustic_output_verified` remain false until there is a separate basis for asserting those properties. Held receiver connections can report `opened` even when the phone is paused.

Since worker 0.2.1, Bluetooth open/retry waits run outside the manager's metadata lock. `receiver.close` and `session.stop` cancel pending attempts before releasing the connections. Poll for `opening` → `opened`, `closed` or `unavailable`; a failed asynchronous attempt reports its error in receiver status rather than retroactively changing the command response. A new `hello.instance_id` means queued commands from an earlier worker must be discarded; the desktop does this and requires a new explicit playback action.

The backend does not set Windows default endpoints and does not ship/install a virtual driver. For receiver routing choose the bridge playback side in Windows and verify the actual path. System-wide loopback can include all apps on that endpoint. A physical loopback source is already audible before SoundShare; use a virtual source for control of delay on every physical output.

Pipe security: owner user SID DACL, session-specific default name, remote clients rejected, first-instance protection, one connected client at a time. Reuse the connection for repeated commands or close it after a response. Idle reads have a timeout; clients reconnect as needed. Do not send credentials or audio media over the command channel.

Typical errors: `invalid_json`, `invalid_argument`, `protocol_version`, `unknown_method`, `invalid_config`, `feedback_route`, `device_unavailable`, `already_running`, `not_running`, `not_found`, `receiver_timeout`, `receiver_open_failed`, `windows_error`, `file_error`.
