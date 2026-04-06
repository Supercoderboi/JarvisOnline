# ESP32 OTA Web App

This is a local web app that runs on Windows and helps you:

- paste or upload ESP32 Arduino code
- compile it into a firmware `.bin` using `arduino-cli`
- upload an existing `.bin` file
- send the compiled or uploaded `.bin` to your existing ESP32 OTA page at `http://192.168.1.64`

The goal is to work with your current ESP32 OTA setup without changing the old firmware code.

## Recommended Online Architecture

If you want to host the UI on `Netlify`, do not try to compile on Netlify itself.

Use this split instead:

- `Netlify`: hosts the OTA website UI
- `GitHub Actions`: compiles the sketch into `.bin`
- `ESP32`: receives the `.bin` through the existing OTA page at `http://192.168.1.64`

This repo now includes a GitHub Actions workflow at:

- [.github/workflows/build-jarvis.yml](C:\Users\Ethean\Desktop\random projects\codextest\web-ota-app\.github\workflows\build-jarvis.yml)

That workflow can:

- use the existing repo sketch
- or accept a base64-encoded sketch source through `workflow_dispatch`
- compile the firmware with the same Arduino core and libraries you were already using
- publish the `.bin` as a GitHub Actions artifact

## What It Assumes

- Your ESP32 already exposes a browser-based OTA page
- The OTA page accepts a multipart file upload
- `arduino-cli` is installed on Windows
- The ESP32 core and libraries from your old GitHub workflow are already installed locally

## Default Target

- Base URL: `http://192.168.1.64`
- Default FQBN: `esp32:esp32:esp32:PartitionScheme=min_spiffs`

## Run

From this folder:

```powershell
python app.py
```

Then open:

`http://127.0.0.1:5000`

## GitHub Mode

If you want compilation to happen on GitHub instead of on your PC:

1. Push this repo to GitHub with [.github/workflows/build-jarvis.yml](C:\Users\Ethean\Desktop\random projects\codextest\web-ota-app\.github\workflows\build-jarvis.yml) in place
2. Trigger it with `workflow_dispatch`
3. Pass your pasted sketch as `source_b64`
4. Download the generated `.bin` artifact
5. Use this OTA web app to upload that `.bin` to the ESP32

## Important Security Note

If the website is hosted publicly on Netlify, do not put a GitHub personal access token directly in the frontend JavaScript.

To trigger GitHub builds safely from a public site, you still need one of:

- a tiny backend proxy you control
- Netlify server-side function with encrypted environment variables
- manual workflow dispatch from GitHub

## Features

- Paste sketch source code into the browser
- Upload a `.ino`, `.cpp`, or `.bin`
- Compile pasted or uploaded source into `.bin` with `arduino-cli`
- Detect the OTA upload form from the existing ESP32 page when possible
- Manually override the OTA action path and file field name if autodetection fails
- Reuse the last compiled firmware for upload

## Matching Your Old GitHub Build

The web app now targets the same style of compile command as your workflow:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs Jarvis/Jarvis.ino --export-binaries
```

That means your local machine needs the same environment installed first:

```powershell
arduino-cli core update-index --additional-urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core install esp32:esp32@2.0.17 --additional-urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli lib install "ArduinoJson" "DHT sensor library" "Adafruit GFX Library" "Adafruit PCD8544 Nokia 5110 LCD library"
```

And these two libraries still need to exist in your Arduino libraries folder:

- `ESP32-BLE-Keyboard`
- `NimBLE-Arduino`

## Common OTA Defaults

Many ESP32 OTA web pages use one of these action paths:

- `/update`
- `/ota`
- `/upload`

Common multipart field names:

- `update`
- `firmware`
- `file`

If autodetection does not find the correct values, enter them manually in the web UI.

## Important Constraint

Compilation now depends on `arduino-cli` and the same ESP32 core/libraries your GitHub build used. If `arduino-cli` is not installed, or those libraries are missing, compilation will fail until the local toolchain matches your old workflow.

If you switch to GitHub Actions for compilation, the local PC no longer needs the full compile toolchain. It only needs to upload the final `.bin` to the ESP32 OTA page.
