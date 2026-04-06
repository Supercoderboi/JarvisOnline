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

Netlify-specific files in this repo:

- [netlify.toml](C:\Users\Ethean\Desktop\random projects\codextest\web-ota-app\netlify.toml)
- [site/index.html](C:\Users\Ethean\Desktop\random projects\codextest\web-ota-app\site\index.html)
- [netlify/functions/dispatch-build.js](C:\Users\Ethean\Desktop\random projects\codextest\web-ota-app\netlify\functions\dispatch-build.js)
- [netlify/functions/build-status.js](C:\Users\Ethean\Desktop\random projects\codextest\web-ota-app\netlify\functions\build-status.js)

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
2. Set these Netlify environment variables:
   - `GITHUB_TOKEN`
   - `GITHUB_OWNER`
   - `GITHUB_REPO`
   - `GITHUB_WORKFLOW_FILE`
   - `GITHUB_REF`
3. Deploy this repo to Netlify
4. Open the deployed site
5. Paste your sketch and trigger the GitHub build
6. Open the workflow run from the status panel and download the artifact
7. Use your OTA upload flow with the generated `.bin`

## Important Security Note

If the website is hosted publicly on Netlify, do not put a GitHub personal access token directly in the frontend JavaScript.

To trigger GitHub builds safely from a public site, you still need one of:

- a tiny backend proxy you control
- Netlify server-side function with encrypted environment variables
- manual workflow dispatch from GitHub

This repo now includes the `Netlify server-side function` approach.

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

## Netlify Environment Variables

Set these in the Netlify site configuration:

- `GITHUB_TOKEN`
  Use a GitHub personal access token with permission to trigger workflows and read actions.
- `GITHUB_OWNER`
  Example: `Supercoderboi`
- `GITHUB_REPO`
  Example: `JarvisOnline`
- `GITHUB_WORKFLOW_FILE`
  Example: `build-jarvis.yml`
- `GITHUB_REF`
  Example: `master`

## Residual Limitation

The Netlify site can trigger builds and inspect workflow status, but this repo does not yet implement artifact download-and-extract or direct browser upload of the artifact `.bin` to the ESP32. The current online path is:

1. Trigger build from the site
2. Open the GitHub workflow run
3. Download the artifact zip
4. Extract the `.bin`
5. Upload it to the ESP32 OTA page
