const { json } = require("./utils");
const { getDraftFile } = require("./draft-utils");

exports.handler = async (event) => {
  if (event.httpMethod === "OPTIONS") {
    return json(200, { ok: true });
  }

  if (event.httpMethod !== "GET") {
    return json(405, { error: "Method not allowed" });
  }

  try {
    const sketchName = String(event.queryStringParameters?.sketch_name || "Jarvis").trim() || "Jarvis";
    const draft = await getDraftFile(sketchName);

    if (!draft.found) {
      return json(404, {
        found: false,
        sketch_name: sketchName,
        path: draft.path
      });
    }

    return json(200, {
      found: true,
      sketch_name: sketchName,
      path: draft.path,
      source_code: draft.content,
      sha: draft.sha
    });
  } catch (error) {
    return json(500, { error: error.message });
  }
};
