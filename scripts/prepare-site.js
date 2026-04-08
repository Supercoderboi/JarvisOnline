const fs = require("fs");
const path = require("path");

const rootDir = path.resolve(__dirname, "..");
const sourceManifest = path.join(rootDir, "firmware", "manifest.json");
const targetDir = path.join(rootDir, "site", "firmware");
const targetManifest = path.join(targetDir, "manifest.json");

fs.mkdirSync(targetDir, { recursive: true });
fs.copyFileSync(sourceManifest, targetManifest);

console.log(`Copied ${sourceManifest} -> ${targetManifest}`);
