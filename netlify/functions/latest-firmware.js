const { githubConfig, githubHeaders, json } = require("./utils");

exports.handler = async (event) => {
  if (event.httpMethod === "OPTIONS") {
    return json(200, { ok: true });
  }

  if (event.httpMethod !== "GET") {
    return json(405, { error: "Method not allowed" });
  }

  try {
    const config = githubConfig();
    const releaseUrl = `https://api.github.com/repos/${config.owner}/${config.repo}/releases/tags/latest-firmware`;
    const releaseResponse = await fetch(releaseUrl, {
      headers: githubHeaders(config.token)
    });

    if (!releaseResponse.ok) {
      const text = await releaseResponse.text();
      return json(releaseResponse.status, { error: `Latest firmware lookup failed: ${text}` });
    }

    const release = await releaseResponse.json();
    const asset = (release.assets || []).find((item) => item.name === "Jarvis.ino.bin") || release.assets?.[0];
    if (!asset) {
      return json(404, { error: "No firmware asset found in latest-firmware release." });
    }

    const assetResponse = await fetch(asset.url, {
      headers: {
        ...githubHeaders(config.token),
        Accept: "application/octet-stream"
      }
    });

    if (!assetResponse.ok) {
      const text = await assetResponse.text();
      return json(assetResponse.status, { error: `Firmware download failed: ${text}` });
    }

    const fileBuffer = Buffer.from(await assetResponse.arrayBuffer());
    return {
      statusCode: 200,
      isBase64Encoded: true,
      headers: {
        "Content-Type": "application/octet-stream",
        "Content-Disposition": 'attachment; filename="Jarvis.ino.bin"',
        "Access-Control-Allow-Origin": "*"
      },
      body: fileBuffer.toString("base64")
    };
  } catch (error) {
    return json(500, { error: error.message });
  }
};
