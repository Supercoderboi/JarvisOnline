const fs = require("fs");
const path = require("path");

const rootDir = path.resolve(__dirname, "..");
const sourceManifest = path.join(rootDir, "firmware", "manifest.json");
const sourceSketch = path.join(rootDir, "firmware", "Jarvis", "Jarvis.ino");
const targetDir = path.join(rootDir, "site", "firmware");
const targetManifest = path.join(targetDir, "manifest.json");
const targetSketch = path.join(targetDir, "Jarvis.ino");

fs.mkdirSync(targetDir, { recursive: true });
fs.copyFileSync(sourceManifest, targetManifest);
fs.copyFileSync(sourceSketch, targetSketch);

console.log(`Copied ${sourceManifest} -> ${targetManifest}`);
console.log(`Copied ${sourceSketch} -> ${targetSketch}`);
