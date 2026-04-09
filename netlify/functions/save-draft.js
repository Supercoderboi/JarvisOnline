const { githubHeaders, json } = require("./utils");
const { getDraftFile } = require("./draft-utils");

exports.handler = async (event) => {
  if (event.httpMethod === "OPTIONS") {
    return json(200, { ok: true });
  }

  if (event.httpMethod !== "POST") {
    return json(405, { error: "Method not allowed" });
  }

  try {
    const payload = JSON.parse(event.body || "{}");
    const sketchName = String(payload.sketch_name || "Jarvis").trim() || "Jarvis";
    const sourceCode = String(payload.source_code || "");

    if (!sourceCode.trim()) {
      return json(400, { error: "source_code is required" });
    }

    const draft = await getDraftFile(sketchName);
    const body = {
      message: `Save ${sketchName} editor draft [skip ci]`,
      content: Buffer.from(sourceCode, "utf8").toString("base64"),
      branch: draft.config.draftRef
    };

    if (draft.found && draft.sha) {
      body.sha = draft.sha;
    }

    const encodedPath = draft.path.split("/").map(encodeURIComponent).join("/");
    const url = `https://api.github.com/repos/${draft.config.owner}/${draft.config.repo}/contents/${encodedPath}`;
    const response = await fetch(url, {
      method: "PUT",
      headers: githubHeaders(draft.config.token),
      body: JSON.stringify(body)
    });

    if (!response.ok) {
      const text = await response.text();
      return json(response.status, { error: `GitHub draft save failed: ${text}` });
    }

    const saved = await response.json();
    return json(200, {
      ok: true,
      saved: true,
      sketch_name: sketchName,
      path: draft.path,
      commit_sha: saved.commit?.sha || ""
    });
  } catch (error) {
    return json(500, { error: error.message });
  }
};
