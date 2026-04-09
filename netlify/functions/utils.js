const required = (name) => {
  const value = process.env[name];
  if (!value) {
    throw new Error(`Missing environment variable: ${name}`);
  }
  return value;
};

const githubConfig = () => ({
  token: required("GITHUB_TOKEN"),
  owner: required("GITHUB_OWNER"),
  repo: required("GITHUB_REPO"),
  workflowFile: process.env.GITHUB_WORKFLOW_FILE || "build-jarvis.yml",
  ref: process.env.GITHUB_REF || "master",
  draftRef: process.env.GITHUB_DRAFT_REF || process.env.GITHUB_REF || "master"
});

const githubHeaders = (token) => ({
  Authorization: `Bearer ${token}`,
  Accept: "application/vnd.github+json",
  "Content-Type": "application/json",
  "User-Agent": "jarvis-online-ota"
});

const json = (statusCode, payload) => ({
  statusCode,
  headers: {
    "Content-Type": "application/json",
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Headers": "Content-Type"
  },
  body: JSON.stringify(payload)
});

module.exports = {
  githubConfig,
  githubHeaders,
  json
};
