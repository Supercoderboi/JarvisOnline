const { githubConfig, githubHeaders, json } = require("./utils");

function pickRun(runs, since) {
  if (!Array.isArray(runs) || runs.length === 0) {
    return null;
  }

  if (!since) {
    return runs[0];
  }

  const sinceTime = Date.parse(since);
  if (Number.isNaN(sinceTime)) {
    return runs[0];
  }

  return runs.find((run) => Date.parse(run.created_at) >= sinceTime) || runs[0];
}

exports.handler = async (event) => {
  if (event.httpMethod === "OPTIONS") {
    return json(200, { ok: true });
  }

  if (event.httpMethod !== "GET") {
    return json(405, { error: "Method not allowed" });
  }

  try {
    const config = githubConfig();
    const since = event.queryStringParameters?.since || "";

    const runsUrl = `https://api.github.com/repos/${config.owner}/${config.repo}/actions/workflows/${config.workflowFile}/runs?event=workflow_dispatch&per_page=10`;
    const runsResponse = await fetch(runsUrl, {
      headers: githubHeaders(config.token)
    });

    if (!runsResponse.ok) {
      const text = await runsResponse.text();
      return json(runsResponse.status, { error: `GitHub runs lookup failed: ${text}` });
    }

    const runsPayload = await runsResponse.json();
    const run = pickRun(runsPayload.workflow_runs || [], since);
    if (!run) {
      return json(200, { ok: true, found: false });
    }

    let artifacts = [];
    let latestFirmware = null;
    if (run.status === "completed") {
      const artifactsResponse = await fetch(run.artifacts_url, {
        headers: githubHeaders(config.token)
      });
      if (artifactsResponse.ok) {
        const artifactsPayload = await artifactsResponse.json();
        artifacts = (artifactsPayload.artifacts || []).map((artifact) => ({
          id: artifact.id,
          name: artifact.name,
          size_in_bytes: artifact.size_in_bytes,
          expired: artifact.expired,
          created_at: artifact.created_at
        }));
      }

      const releaseResponse = await fetch(
        `https://api.github.com/repos/${config.owner}/${config.repo}/releases/tags/latest-firmware`,
        { headers: githubHeaders(config.token) }
      );
      if (releaseResponse.ok) {
        const releasePayload = await releaseResponse.json();
        const asset = (releasePayload.assets || []).find((item) => item.name === "Jarvis.ino.bin") || releasePayload.assets?.[0];
        if (asset) {
          latestFirmware = {
            name: asset.name,
            size_in_bytes: asset.size,
            download_url: asset.browser_download_url
          };
        }
      }
    }

    return json(200, {
      ok: true,
      found: true,
      run: {
        id: run.id,
        name: run.name,
        status: run.status,
        conclusion: run.conclusion,
        html_url: run.html_url,
        created_at: run.created_at,
        updated_at: run.updated_at,
        run_number: run.run_number
      },
      artifacts,
      latest_firmware: latestFirmware
    });
  } catch (error) {
    return json(500, { error: error.message });
  }
};
