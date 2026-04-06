from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import textwrap
import uuid
from dataclasses import dataclass
from html.parser import HTMLParser
from pathlib import Path
from typing import Optional
from urllib import error, parse, request

from flask import Flask, redirect, render_template_string, request as flask_request, send_file, url_for


APP_DIR = Path(__file__).resolve().parent
WORK_DIR = APP_DIR / "workspace"
BUILD_ROOT = WORK_DIR / "builds"
UPLOAD_ROOT = WORK_DIR / "uploads"
LAST_BIN = WORK_DIR / "last_firmware.bin"
SETTINGS_PATH = APP_DIR / "settings.json"

WORK_DIR.mkdir(exist_ok=True)
BUILD_ROOT.mkdir(exist_ok=True)
UPLOAD_ROOT.mkdir(exist_ok=True)

app = Flask(__name__)


DEFAULT_SETTINGS = {
    "target_url": "http://192.168.1.64",
    "board_fqbn": "esp32:esp32:esp32:PartitionScheme=min_spiffs",
    "sketch_name": "Jarvis",
    "additional_urls": "https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json",
    "upload_action": "",
    "upload_field_name": "",
    "build_flags": "",
}


LIBRARY_HEADER_MAP = {
    "ArduinoJson.h": {"type": "arduino-cli", "name": "ArduinoJson"},
    "DHT.h": {"type": "arduino-cli", "name": "DHT sensor library"},
    "Adafruit_GFX.h": {"type": "arduino-cli", "name": "Adafruit GFX Library"},
    "Adafruit_PCD8544.h": {"type": "arduino-cli", "name": "Adafruit PCD8544 Nokia 5110 LCD library"},
    "BleKeyboard.h": {
        "type": "git",
        "name": "ESP32-BLE-Keyboard",
        "repo": "https://github.com/T-vK/ESP32-BLE-Keyboard.git",
    },
    "NimBLEDevice.h": {
        "type": "git",
        "name": "NimBLE-Arduino",
        "repo": "https://github.com/h2zero/NimBLE-Arduino.git",
    },
    "NimBLEUtils.h": {
        "type": "git",
        "name": "NimBLE-Arduino",
        "repo": "https://github.com/h2zero/NimBLE-Arduino.git",
    },
    "NimBLEServer.h": {
        "type": "git",
        "name": "NimBLE-Arduino",
        "repo": "https://github.com/h2zero/NimBLE-Arduino.git",
    },
}


HTML_TEMPLATE = """
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 OTA Builder</title>
  <style>
    :root {
      --bg: #f1eee4;
      --panel: #fffaf0;
      --ink: #1e2b2f;
      --muted: #5b6a6f;
      --accent: #0d6b5f;
      --accent-2: #d38d1b;
      --line: #d8d2c0;
      --error: #9f2f2f;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Segoe UI", Tahoma, sans-serif;
      background:
        radial-gradient(circle at top left, rgba(211,141,27,0.12), transparent 30%),
        linear-gradient(180deg, #f7f3e8 0%, var(--bg) 100%);
      color: var(--ink);
    }
    .wrap {
      max-width: 1200px;
      margin: 0 auto;
      padding: 24px;
    }
    .hero {
      display: flex;
      justify-content: space-between;
      gap: 16px;
      align-items: end;
      margin-bottom: 20px;
    }
    .hero h1 {
      margin: 0;
      font-size: 2rem;
    }
    .hero p {
      margin: 6px 0 0;
      color: var(--muted);
    }
    .grid {
      display: grid;
      grid-template-columns: 1.6fr 1fr;
      gap: 18px;
    }
    .panel {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 18px;
      padding: 18px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.04);
    }
    .panel h2 {
      margin: 0 0 12px;
      font-size: 1.1rem;
    }
    label {
      display: block;
      margin: 10px 0 6px;
      font-weight: 600;
    }
    input[type=text], textarea, select {
      width: 100%;
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 10px 12px;
      font: inherit;
      background: white;
    }
    textarea {
      min-height: 360px;
      resize: vertical;
      font-family: Consolas, "Courier New", monospace;
      font-size: 14px;
    }
    .row {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 12px;
    }
    .actions {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      margin-top: 14px;
    }
    button {
      border: 0;
      border-radius: 999px;
      background: var(--accent);
      color: white;
      padding: 11px 16px;
      font: inherit;
      cursor: pointer;
    }
    button.secondary {
      background: var(--accent-2);
      color: #1e1605;
    }
    .status {
      padding: 12px 14px;
      border-radius: 12px;
      margin-bottom: 14px;
      background: #eef7f4;
      border: 1px solid #cfe4dd;
    }
    .status.error {
      background: #fbecec;
      border-color: #e7c3c3;
      color: var(--error);
    }
    .meta {
      font-size: 0.95rem;
      color: var(--muted);
      line-height: 1.5;
    }
    pre.log {
      background: #172126;
      color: #e8f5ef;
      padding: 14px;
      border-radius: 12px;
      overflow: auto;
      white-space: pre-wrap;
      min-height: 220px;
    }
    .badge {
      display: inline-block;
      padding: 4px 9px;
      border-radius: 999px;
      background: #e7efe9;
      color: #23443b;
      margin-right: 8px;
      margin-bottom: 8px;
      font-size: 0.88rem;
    }
    @media (max-width: 900px) {
      .grid { grid-template-columns: 1fr; }
      .row { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="hero">
      <div>
        <h1>ESP32 OTA Builder</h1>
        <p>Compile Arduino code into firmware and send it to your existing OTA page.</p>
      </div>
      <div class="meta">
        <span class="badge">Target {{ settings.target_url }}</span>
        <span class="badge">FQBN {{ settings.board_fqbn }}</span>
      </div>
    </div>

    {% if message %}
      <div class="status {% if is_error %}error{% endif %}">{{ message }}</div>
    {% endif %}

    <div class="grid">
      <div class="panel">
        <h2>Source Or Binary</h2>
        <form method="post" action="/action" enctype="multipart/form-data">
          <label for="source_code">ESP32 Arduino source code</label>
          <textarea id="source_code" name="source_code">{{ source_code }}</textarea>

          <label for="source_file">Or upload source file (.ino or .cpp)</label>
          <input id="source_file" type="file" name="source_file" accept=".ino,.cpp,.h,.hpp,.txt">

          <div class="row">
            <div>
              <label for="board_fqbn">Arduino FQBN</label>
              <input id="board_fqbn" type="text" name="board_fqbn" value="{{ settings.board_fqbn }}">
            </div>
            <div>
              <label for="sketch_name">Sketch name</label>
              <input id="sketch_name" type="text" name="sketch_name" value="{{ settings.sketch_name }}">
            </div>
          </div>

          <label for="additional_urls">Additional board manager URLs</label>
          <input id="additional_urls" type="text" name="additional_urls" value="{{ settings.additional_urls }}">

          <div class="actions">
            <button type="submit" name="intent" value="compile">Compile To BIN</button>
            <button class="secondary" type="submit" name="intent" value="analyze_code">Analyze And Install Dependencies</button>
          </div>
        </form>

        <form method="post" action="/action" enctype="multipart/form-data" style="margin-top: 18px;">
          <input type="hidden" name="intent" value="upload_bin">
          <label for="bin_file">Upload an existing firmware .bin</label>
          <input id="bin_file" type="file" name="bin_file" accept=".bin">
          <div class="actions">
            <button class="secondary" type="submit">Use This BIN For OTA</button>
          </div>
        </form>
      </div>

      <div class="panel">
        <h2>OTA Target</h2>
        <form method="post" action="/action">
          <input type="hidden" name="intent" value="save_settings">
          <label for="target_url">ESP32 base URL</label>
          <input id="target_url" type="text" name="target_url" value="{{ settings.target_url }}">

          <div class="row">
            <div>
              <label for="upload_action">Manual upload action path</label>
              <input id="upload_action" type="text" name="upload_action" value="{{ settings.upload_action }}" placeholder="/update">
            </div>
            <div>
              <label for="upload_field_name">Manual file field name</label>
              <input id="upload_field_name" type="text" name="upload_field_name" value="{{ settings.upload_field_name }}" placeholder="update">
            </div>
          </div>

          <div class="actions">
            <button type="submit">Save OTA Settings</button>
          </div>
        </form>

        <form method="post" action="/action" style="margin-top: 18px;">
          <input type="hidden" name="intent" value="detect_ota">
          <div class="actions">
            <button class="secondary" type="submit">Detect OTA Form</button>
          </div>
        </form>

        <form method="post" action="/action" style="margin-top: 18px;">
          <input type="hidden" name="intent" value="submit_ota">
          <div class="actions">
            <button type="submit">Submit Current BIN To ESP32</button>
            {% if has_last_bin %}
            <a href="/last-bin" style="align-self:center;">Download current BIN</a>
            {% endif %}
          </div>
        </form>

        <div class="meta" style="margin-top: 18px;">
          <div>Detected action: <strong>{{ detection.action or "none" }}</strong></div>
          <div>Detected field name: <strong>{{ detection.field_name or "none" }}</strong></div>
          <div>Current firmware: <strong>{{ last_bin_name or "none" }}</strong></div>
        </div>
      </div>
    </div>

    <div class="panel" style="margin-top: 18px;">
      <h2>Build / Upload Log</h2>
      <pre class="log">{{ log_text }}</pre>
    </div>
  </div>
</body>
</html>
"""


@dataclass
class OtaDetection:
    action: str = ""
    field_name: str = ""


class OtaFormParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.forms: list[dict] = []
        self._current_form: Optional[dict] = None

    def handle_starttag(self, tag: str, attrs: list[tuple[str, Optional[str]]]) -> None:
        attr_map = dict(attrs)
        if tag == "form":
            self._current_form = {"action": attr_map.get("action", ""), "inputs": []}
            self.forms.append(self._current_form)
        elif tag == "input" and self._current_form is not None:
            self._current_form["inputs"].append(
                {"type": attr_map.get("type", ""), "name": attr_map.get("name", "")}
            )

    def handle_endtag(self, tag: str) -> None:
        if tag == "form":
            self._current_form = None


def load_settings() -> dict:
    if not SETTINGS_PATH.exists():
        SETTINGS_PATH.write_text(json.dumps(DEFAULT_SETTINGS, indent=2), encoding="utf-8")
        return DEFAULT_SETTINGS.copy()
    payload = json.loads(SETTINGS_PATH.read_text(encoding="utf-8"))
    merged = DEFAULT_SETTINGS.copy()
    merged.update(payload)
    return merged


def save_settings(payload: dict) -> dict:
    merged = DEFAULT_SETTINGS.copy()
    merged.update(payload)
    SETTINGS_PATH.write_text(json.dumps(merged, indent=2), encoding="utf-8")
    return merged


def example_source() -> str:
    return textwrap.dedent(
        """
        #include <Arduino.h>

        void setup() {
          Serial.begin(115200);
        }

        void loop() {
          Serial.println("Jarvis OTA test build");
          delay(1000);
        }
        """
    ).strip()


def run_command(command: list[str], cwd: Optional[Path] = None) -> tuple[int, str, str]:
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            text=True,
            capture_output=True,
            shell=False,
        )
    except FileNotFoundError as exc:
        raise RuntimeError(f"Required tool was not found: {command[0]}") from exc
    output = f"$ {' '.join(command)}\n\n{result.stdout}\n{result.stderr}".strip()
    return result.returncode, output, result.stdout


def find_arduino_cli() -> str:
    cli_path = shutil.which("arduino-cli")
    if cli_path is None:
        raise RuntimeError(
            "arduino-cli is not installed or not in PATH. "
            "Install arduino-cli on Windows, then restart this web app."
        )
    return cli_path


def find_git() -> str:
    git_path = shutil.which("git")
    if git_path is None:
        raise RuntimeError("git is not installed or not in PATH.")
    return git_path


def parse_includes(source_code: str) -> list[str]:
    pattern = re.compile(r'^\s*#include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
    return sorted(set(pattern.findall(source_code)))


def detect_required_dependencies(source_code: str) -> list[dict]:
    headers = parse_includes(source_code)
    dependencies: list[dict] = []
    seen: set[str] = set()
    for header in headers:
        info = LIBRARY_HEADER_MAP.get(header)
        if info is None:
            continue
        key = f'{info["type"]}:{info["name"]}'
        if key in seen:
            continue
        seen.add(key)
        dependency = dict(info)
        dependency["header"] = header
        dependencies.append(dependency)
    return dependencies


def resolve_arduino_libraries_dir() -> Path:
    sketchbook = Path.home() / "Documents" / "Arduino"
    libraries_dir = sketchbook / "libraries"
    libraries_dir.mkdir(parents=True, exist_ok=True)
    return libraries_dir


def ensure_esp32_core(cli_path: str, additional_urls: str) -> str:
    logs: list[str] = []
    command = [cli_path, "core", "list", "--format", "json"]
    code, output, stdout = run_command(command)
    logs.append(output)
    if code != 0:
        raise RuntimeError(output)

    installed = json.loads(stdout or "[]")
    has_esp32 = any(str(item.get("ID", "")) == "esp32:esp32" for item in installed)
    if has_esp32:
        return "\n\n".join(logs)

    install_command = [cli_path, "core", "install", "esp32:esp32@2.0.17"]
    if additional_urls.strip():
        install_command.extend(["--additional-urls", additional_urls.strip()])
    code, output, _ = run_command(install_command)
    logs.append(output)
    if code != 0:
        raise RuntimeError("\n\n".join(logs))
    return "\n\n".join(logs)


def installed_library_names(cli_path: str) -> set[str]:
    code, output, stdout = run_command([cli_path, "lib", "list", "--format", "json"])
    if code != 0:
        raise RuntimeError(output)
    payload = json.loads(stdout or "[]")
    names: set[str] = set()
    for item in payload:
        name = item.get("library", {}).get("name")
        if name:
            names.add(str(name))
    return names


def ensure_dependency_tools(source_code: str, additional_urls: str) -> str:
    cli_path = find_arduino_cli()
    logs: list[str] = []
    logs.append(ensure_esp32_core(cli_path, additional_urls))

    dependencies = detect_required_dependencies(source_code)
    if not dependencies:
        return "\n\n".join([entry for entry in logs if entry]).strip() or "No known external libraries were detected."

    installed_libs = installed_library_names(cli_path)
    for dependency in dependencies:
        if dependency["type"] == "arduino-cli":
            if dependency["name"] in installed_libs:
                logs.append(f'Library already installed: {dependency["name"]}')
                continue
            code, output, _ = run_command([cli_path, "lib", "install", dependency["name"]])
            logs.append(output)
            if code != 0:
                raise RuntimeError("\n\n".join(logs))
            installed_libs.add(dependency["name"])
            continue

        if dependency["type"] == "git":
            git_path = find_git()
            target_dir = resolve_arduino_libraries_dir() / dependency["name"]
            if target_dir.exists():
                logs.append(f'Git library already present: {dependency["name"]}')
                continue
            code, output, _ = run_command([git_path, "clone", dependency["repo"], str(target_dir)])
            logs.append(output)
            if code != 0:
                raise RuntimeError("\n\n".join(logs))

    return "\n\n".join(logs)


def build_project(source_code: str, fqbn: str, sketch_name: str, additional_urls: str) -> tuple[Path, str]:
    cli_path = find_arduino_cli()
    dependency_log = ensure_dependency_tools(source_code, additional_urls)

    build_id = uuid.uuid4().hex
    project_dir = BUILD_ROOT / build_id
    sketch_dir = project_dir / sketch_name
    sketch_dir.mkdir(parents=True, exist_ok=True)
    sketch_path = sketch_dir / f"{sketch_name}.ino"
    sketch_path.write_text(source_code, encoding="utf-8")

    command = [cli_path, "compile", "--fqbn", fqbn]
    if additional_urls.strip():
        command.extend(["--additional-urls", additional_urls.strip()])
    command.extend(["--export-binaries", str(sketch_path)])
    code, build_output, _ = run_command(command, cwd=project_dir)
    log = "\n\n".join([part for part in [dependency_log, build_output] if part]).strip()
    if code != 0:
        raise RuntimeError(log)

    build_dir = sketch_dir / "build"
    firmware_candidates = sorted(build_dir.rglob("*.bin"))
    firmware_path = next((path for path in firmware_candidates if path.name.endswith(".bin")), None)
    if firmware_path is None:
        raise RuntimeError(f"Build succeeded but firmware not found.\n\n{log}")

    shutil.copyfile(firmware_path, LAST_BIN)
    return LAST_BIN, log


def read_uploaded_text(upload) -> str:
    raw = upload.read()
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise RuntimeError("Source file must be UTF-8 text.") from exc


def save_uploaded_bin(upload) -> Path:
    if not upload or not upload.filename:
        raise RuntimeError("No .bin file was provided.")
    target = UPLOAD_ROOT / f"{uuid.uuid4().hex}-{Path(upload.filename).name}"
    upload.save(target)
    shutil.copyfile(target, LAST_BIN)
    return LAST_BIN


def normalize_action(base_url: str, action: str) -> str:
    if not action:
        return ""
    return parse.urljoin(base_url.rstrip("/") + "/", action)


def detect_ota_form(base_url: str) -> OtaDetection:
    try:
        with request.urlopen(base_url, timeout=5) as response:
            html_text = response.read().decode("utf-8", errors="replace")
    except error.URLError as exc:
        raise RuntimeError(f"Could not open OTA page: {exc}") from exc

    parser = OtaFormParser()
    parser.feed(html_text)

    for form in parser.forms:
        file_inputs = [item for item in form["inputs"] if item["type"].lower() == "file"]
        if file_inputs:
            chosen = file_inputs[0]
            return OtaDetection(
                action=form.get("action", "") or "",
                field_name=chosen.get("name", "") or "",
            )

    raise RuntimeError("No upload form with a file input was found on the page.")


def encode_multipart(field_name: str, filename: str, file_bytes: bytes) -> tuple[bytes, str]:
    boundary = f"----JarvisBoundary{uuid.uuid4().hex}"
    body = bytearray()
    body.extend(f"--{boundary}\r\n".encode("utf-8"))
    disposition = f'Content-Disposition: form-data; name="{field_name}"; filename="{filename}"\r\n'
    body.extend(disposition.encode("utf-8"))
    body.extend(b"Content-Type: application/octet-stream\r\n\r\n")
    body.extend(file_bytes)
    body.extend(f"\r\n--{boundary}--\r\n".encode("utf-8"))
    return bytes(body), boundary


def submit_ota(base_url: str, action: str, field_name: str, bin_path: Path) -> str:
    if not bin_path.exists():
        raise RuntimeError("No firmware .bin is available yet.")
    if not field_name:
        raise RuntimeError("Upload field name is missing.")
    upload_url = normalize_action(base_url, action or "/update")
    file_bytes = bin_path.read_bytes()
    body, boundary = encode_multipart(field_name, bin_path.name, file_bytes)
    req = request.Request(
        upload_url,
        data=body,
        headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
        method="POST",
    )
    try:
        with request.urlopen(req, timeout=20) as response:
            payload = response.read().decode("utf-8", errors="replace")
    except error.URLError as exc:
        raise RuntimeError(f"OTA upload failed: {exc}") from exc

    return f"Submitted {bin_path.name} to {upload_url}\n\nResponse:\n{payload}"


def render_page(message: str = "", is_error: bool = False, log_text: str = "", source_code: str = ""):
    settings = load_settings()
    detection = OtaDetection(
        action=settings.get("upload_action", ""),
        field_name=settings.get("upload_field_name", ""),
    )
    return render_template_string(
        HTML_TEMPLATE,
        settings=settings,
        message=message,
        is_error=is_error,
        log_text=log_text,
        source_code=source_code or example_source(),
        detection=detection,
        has_last_bin=LAST_BIN.exists(),
        last_bin_name=LAST_BIN.name if LAST_BIN.exists() else "",
    )


@app.get("/")
def index():
    return render_page()


@app.get("/last-bin")
def last_bin():
    if not LAST_BIN.exists():
        return redirect(url_for("index"))
    return send_file(LAST_BIN, as_attachment=True, download_name=LAST_BIN.name)


@app.post("/action")
def action():
    intent = flask_request.form.get("intent", "")
    settings = load_settings()

    try:
        if intent == "save_settings":
            settings = save_settings(
                {
                    "target_url": flask_request.form.get("target_url", settings["target_url"]).strip(),
                    "upload_action": flask_request.form.get("upload_action", "").strip(),
                    "upload_field_name": flask_request.form.get("upload_field_name", "").strip(),
                    "board_fqbn": settings["board_fqbn"],
                    "sketch_name": settings["sketch_name"],
                    "additional_urls": settings["additional_urls"],
                    "build_flags": settings.get("build_flags", ""),
                }
            )
            return render_page("OTA settings saved.", False)

        if intent == "detect_ota":
            detection = detect_ota_form(settings["target_url"])
            settings = save_settings(
                {
                    **settings,
                    "upload_action": detection.action,
                    "upload_field_name": detection.field_name,
                }
            )
            msg = f"Detected action '{detection.action or '/update'}' and field '{detection.field_name or 'unknown'}'."
            return render_page(msg, False)

        if intent == "compile":
            source_code = flask_request.form.get("source_code", "").strip()
            source_upload = flask_request.files.get("source_file")
            if source_upload and source_upload.filename:
                source_code = read_uploaded_text(source_upload)
            if not source_code:
                raise RuntimeError("Provide source code or upload a source file first.")

            board_fqbn = flask_request.form.get("board_fqbn", settings["board_fqbn"]).strip() or settings["board_fqbn"]
            sketch_name = flask_request.form.get("sketch_name", settings["sketch_name"]).strip() or settings["sketch_name"]
            additional_urls = flask_request.form.get("additional_urls", settings["additional_urls"]).strip()
            settings = save_settings(
                {
                    **settings,
                    "board_fqbn": board_fqbn,
                    "sketch_name": sketch_name,
                    "additional_urls": additional_urls,
                }
            )
            _, log_text = build_project(source_code, board_fqbn, sketch_name, additional_urls)
            return render_page("Compilation succeeded. Current BIN is ready for OTA.", False, log_text, source_code)

        if intent == "analyze_code":
            source_code = flask_request.form.get("source_code", "").strip()
            source_upload = flask_request.files.get("source_file")
            if source_upload and source_upload.filename:
                source_code = read_uploaded_text(source_upload)
            if not source_code:
                raise RuntimeError("Provide source code or upload a source file first.")

            additional_urls = flask_request.form.get("additional_urls", settings["additional_urls"]).strip()
            log_text = ensure_dependency_tools(source_code, additional_urls)
            headers = parse_includes(source_code)
            prefix = "Detected headers:\n" + "\n".join(headers) if headers else "No #include headers were detected."
            return render_page("Dependency analysis completed.", False, f"{prefix}\n\n{log_text}", source_code)

        if intent == "upload_bin":
            upload = flask_request.files.get("bin_file")
            saved = save_uploaded_bin(upload)
            return render_page(f"Stored {saved.name} as the current firmware BIN.", False)

        if intent == "submit_ota":
            upload_action = settings.get("upload_action", "")
            field_name = settings.get("upload_field_name", "")
            if not field_name:
                try:
                    detection = detect_ota_form(settings["target_url"])
                    upload_action = detection.action or upload_action
                    field_name = detection.field_name or field_name
                    save_settings(
                        {
                            **settings,
                            "upload_action": upload_action,
                            "upload_field_name": field_name,
                        }
                    )
                except Exception:
                    pass

            log_text = submit_ota(settings["target_url"], upload_action, field_name, LAST_BIN)
            return render_page("OTA upload submitted.", False, log_text)

        raise RuntimeError("Unknown action.")
    except Exception as exc:
        source_code = flask_request.form.get("source_code", "").strip() or example_source()
        return render_page(str(exc), True, str(exc), source_code)


if __name__ == "__main__":
    app.run(host="127.0.0.1", port=5000, debug=False)
