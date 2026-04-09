const { githubConfig, githubHeaders } = require("./utils");

const githubPath = (path) => path.split("/").map(encodeURIComponent).join("/");

const draftPathFor = (sketchName) => {
  const safeName = String(sketchName || "Jarvis").trim() || "Jarvis";
  return `.drafts/${safeName}.ino`;
};

const getDraftFile = async (sketchName) => {
  const config = githubConfig();
  const path = draftPathFor(sketchName);
  const url = `https://api.github.com/repos/${config.owner}/${config.repo}/contents/${githubPath(path)}?ref=${encodeURIComponent(config.draftRef)}`;
  const response = await fetch(url, {
    headers: githubHeaders(config.token)
  });

  if (response.status === 404) {
    return { found: false, path, config };
  }

  if (!response.ok) {
    const text = await response.text();
    throw new Error(`GitHub draft lookup failed: ${text}`);
  }

  const payload = await response.json();
  const content = Buffer.from(payload.content || "", "base64").toString("utf8");
  return {
    found: true,
    path,
    config,
    sha: payload.sha,
    content
  };
};

module.exports = {
  draftPathFor,
  getDraftFile
};
