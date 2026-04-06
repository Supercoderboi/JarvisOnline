const { githubConfig, githubHeaders, json } = require("./utils");

exports.handler = async (event) => {
  if (event.httpMethod === "OPTIONS") {
    return json(200, { ok: true });
  }

  if (event.httpMethod !== "POST") {
    return json(405, { error: "Method not allowed" });
  }

  try {
    const config = githubConfig();
    const payload = JSON.parse(event.body || "{}");
    const sourceCode = String(payload.source_code || "").trim();
    const sketchName = String(payload.sketch_name || "Jarvis").trim() || "Jarvis";

    if (!sourceCode) {
      return json(400, { error: "source_code is required" });
    }

    const sourceB64 = Buffer.from(sourceCode, "utf8").toString("base64");
    const dispatchBody = {
      ref: config.ref,
      inputs: {
        source_b64: sourceB64,
        sketch_name: sketchName
      }
    };

    const dispatchUrl = `https://api.github.com/repos/${config.owner}/${config.repo}/actions/workflows/${config.workflowFile}/dispatches`;
    const response = await fetch(dispatchUrl, {
      method: "POST",
      headers: githubHeaders(config.token),
      body: JSON.stringify(dispatchBody)
    });

    if (!response.ok) {
      const text = await response.text();
      return json(response.status, { error: `GitHub dispatch failed: ${text}` });
    }

    return json(200, {
      ok: true,
      queued: true,
      workflow_file: config.workflowFile,
      sketch_name: sketchName,
      dispatched_at: new Date().toISOString()
    });
  } catch (error) {
    return json(500, { error: error.message });
  }
};
