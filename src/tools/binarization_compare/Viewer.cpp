// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Viewer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace binarization_compare {
namespace {
// The page is in English, like the output of all developer tools.
const char* const kIndexHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Binarization Comparison</title>
<style>
:root { --bg: #f4f4f2; --panel: #fff; --text: #222; --muted: #666; --line: #d6d6d0;
  --ours: #2a6fb0; --experimental: #b0602a; --filter: #5a8a3a; --accent: #3399ff; }
@media (prefers-color-scheme: dark) {
  :root { --bg: #1d1d1b; --panel: #2a2a28; --text: #e8e8e4; --muted: #a0a09a; --line: #44443f; }
}
* { box-sizing: border-box; }
body { margin: 0; font: 14px/1.4 system-ui, sans-serif; background: var(--bg); color: var(--text); }
header { display: flex; flex-wrap: wrap; gap: 12px; align-items: center; padding: 10px 16px;
  background: var(--panel); border-bottom: 1px solid var(--line); position: sticky; top: 0; z-index: 2; }
header h1 { font-size: 16px; margin: 0 12px 0 0; }
select, button { font: inherit; padding: 4px 8px; }
main { display: grid; grid-template-columns: 300px 1fr; gap: 16px; padding: 16px; }
@media (max-width: 800px) { main { grid-template-columns: 1fr; } }
#side { position: sticky; top: 64px; align-self: start; }
#overview { position: relative; cursor: crosshair; border: 1px solid var(--line); background: #fff; }
#overview img { display: block; width: 100%; }
#frame { position: absolute; border: 2px solid var(--accent); pointer-events: none; }
#info { color: var(--muted); margin: 8px 0; }
#methods label { display: block; margin: 2px 0; }
#methods h3 { font-size: 13px; margin: 12px 0 4px; }
#grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(var(--tile-w), 1fr)); gap: 12px; }
.tile { background: var(--panel); border: 1px solid var(--line); border-top: 4px solid var(--g); }
.tile .head { padding: 4px 8px; display: flex; justify-content: space-between; gap: 8px; }
.tile .head b { white-space: nowrap; }
.tile .head span { color: var(--muted); font-size: 12px; text-align: right; }
.tile .view { height: var(--tile-h); background-color: #fff; background-repeat: no-repeat;
  image-rendering: pixelated; cursor: zoom-in; }
.hint { color: var(--muted); font-size: 12px; }
</style>
</head>
<body>
<header>
  <h1 id="title">Binarization Comparison</h1>
  <label>Page <select id="page"></select></label>
  <label>Zoom <select id="zoom">
    <option value="0.5">50 %</option><option value="1" selected>100 %</option>
    <option value="2">200 %</option><option value="3">300 %</option></select></label>
  <label>Tile <select id="tile">
    <option value="360x260">small</option><option value="480x340" selected>medium</option>
    <option value="640x460">large</option></select></label>
  <span class="hint">Click the overview or press the arrow keys to move the view. Click a tile to see the whole image.</span>
</header>
<main>
  <div id="side">
    <div id="overview"><img id="ovimg" alt=""><div id="frame"></div></div>
    <div id="info"></div>
    <div id="methods"></div>
  </div>
  <div id="grid"></div>
</main>
<script src="data.js"></script>
<script>
const $ = (id) => document.getElementById(id);
const isColors = DATA.mode === "colors";
const title = isColors ? "Colored Text Comparison" : "Binarization Comparison";
document.title = title;
$("title").textContent = title;
const GROUPS = isColors
  ? { original: ["Original (color)", "#888"], ours: ["ScanTailor OCR", "var(--ours)"],
      experimental: ["K-Means (own code, idea from ScanTailor Experimental)", "var(--experimental)"],
      filter: ["K-Means (original code of ScanTailor Experimental)", "var(--filter)"] }
  : { original: ["Original (grayscale)", "#888"], ours: ["ScanTailor OCR", "var(--ours)"],
      experimental: ["ScanTailor Experimental", "var(--experimental)"],
      filter: ["Filter (grayscale)", "var(--filter)"] };
const store = {
  get(k, d) { try { const v = localStorage.getItem("bc_" + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } },
  set(k, v) { try { localStorage.setItem("bc_" + k, JSON.stringify(v)); } catch (e) {} }
};
let page = null, cx = 0, cy = 0;
const savedHidden = store.get("hidden", []);
const hidden = new Set(Array.isArray(savedHidden) ? savedHidden : []);

function results(p) {
  return [{ id: "original", label: "Original", group: "original", parameters: "", ms: null, file: "original.jpg" }]
    .concat(Array.isArray(p.results) ? p.results : []);
}
function restoreSelect(el, key, fallback) {
  const saved = store.get(key, fallback);
  const valid = Array.from(el.options).some((o) => o.value === String(saved));
  el.value = valid ? String(saved) : fallback;
}
function tileSize() {
  const parts = $("tile").value.split("x").map(Number);
  return (parts.length === 2 && Number.isFinite(parts[0]) && parts[0] > 0 && Number.isFinite(parts[1]) && parts[1] > 0)
    ? parts : [480, 340];
}
function zoom() {
  const z = Number($("zoom").value);
  return (Number.isFinite(z) && z > 0) ? z : 1;
}

function buildMethods() {
  const box = $("methods"); box.innerHTML = "";
  const seen = new Set();
  const byGroup = new Map();
  for (const p of DATA.pages) {
    for (const r of results(p)) {
      if (seen.has(r.id)) continue;
      seen.add(r.id);
      if (!byGroup.has(r.group)) byGroup.set(r.group, []);
      byGroup.get(r.group).push(r);
    }
  }
  for (const [group, items] of byGroup.entries()) {
    const h = document.createElement("h3"); h.textContent = (GROUPS[group] || [group])[0]; box.appendChild(h);
    for (const r of items) {
      const l = document.createElement("label");
      const c = document.createElement("input"); c.type = "checkbox"; c.checked = !hidden.has(r.id);
      c.onchange = () => { c.checked ? hidden.delete(r.id) : hidden.add(r.id); store.set("hidden", [...hidden]); render(); };
      l.append(c, " " + r.label); box.appendChild(l);
    }
  }
}

function selectPage(id) {
  page = DATA.pages.find((p) => p.id === id) || DATA.pages[0] || null;
  if (!page) return;
  store.set("page", page.id);
  $("page").value = page.id;
  cx = page.width / 2; cy = page.height / 3;
  $("ovimg").src = page.id + "/overview.jpg";
  $("info").textContent = page.file + " · " + page.width + "×" + page.height + " px" + (page.dpi ? " · " + page.dpi + " dpi" : "");
  render();
}

function render() {
  if (!page) return;
  const [tw, th] = tileSize(), z = zoom();
  const grid = $("grid");
  grid.style.setProperty("--tile-w", tw + "px"); grid.style.setProperty("--tile-h", th + "px");
  grid.innerHTML = "";
  for (const r of results(page)) {
    if (hidden.has(r.id)) continue;
    const t = document.createElement("div"); t.className = "tile";
    t.style.setProperty("--g", (GROUPS[r.group] || [r.group, "#888"])[1]);
    const head = document.createElement("div"); head.className = "head";
    const b = document.createElement("b"); b.textContent = r.label;
    const s = document.createElement("span");
    s.textContent = [r.parameters, r.ms === null ? "" : (r.ms / 1000).toFixed(2) + " s"].filter(Boolean).join(" · ");
    head.append(b, s);
    const v = document.createElement("div"); v.className = "view";
    v.style.backgroundImage = "url('" + page.id + "/" + r.file + "')";
    v.style.backgroundSize = page.width * z + "px " + page.height * z + "px";
    t.append(head, v); grid.appendChild(t);
    v.onclick = () => window.open(page.id + "/" + r.file, "_blank");
  }
  position();
}

function position() {
  if (!page || !(page.width > 0) || !(page.height > 0)) return;
  const [tw0, th] = tileSize(), z = zoom();
  const views = document.querySelectorAll(".view");
  const tw = (views.length && views[0].clientWidth > 0) ? views[0].clientWidth : tw0;
  const w = tw / z, h = th / z;
  if (!Number.isFinite(cx)) cx = page.width / 2;
  if (!Number.isFinite(cy)) cy = page.height / 3;
  cx = Math.min(Math.max(cx, w / 2), Math.max(page.width - w / 2, w / 2));
  cy = Math.min(Math.max(cy, h / 2), Math.max(page.height - h / 2, h / 2));
  for (const v of views) v.style.backgroundPosition = -(cx - w / 2) * z + "px " + -(cy - h / 2) * z + "px";
  const ov = $("ovimg"), f = $("frame");
  if (ov.clientWidth > 0) {
    const s = ov.clientWidth / page.width;
    f.style.left = (cx - w / 2) * s + "px"; f.style.top = (cy - h / 2) * s + "px";
    f.style.width = w * s + "px"; f.style.height = h * s + "px";
  }
}

$("overview").onclick = (e) => {
  if (!page || !(page.width > 0)) return;
  const rect = $("ovimg").getBoundingClientRect();
  if (!(rect.width > 0)) return;
  const s = rect.width / page.width;
  cx = (e.clientX - rect.left) / s; cy = (e.clientY - rect.top) / s; position();
};
document.addEventListener("keydown", (e) => {
  if (!page || e.target.tagName === "SELECT") return;
  const [tw, th] = tileSize(), z = zoom(), step = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key];
  if (!step) return;
  e.preventDefault(); cx += step[0] * tw / z / 2; cy += step[1] * th / z / 2; position();
});
$("zoom").onchange = () => { store.set("zoom", $("zoom").value); render(); };
$("tile").onchange = () => { store.set("tile", $("tile").value); render(); };
$("page").onchange = () => selectPage($("page").value);
$("ovimg").onload = position;
window.onresize = position;

restoreSelect($("zoom"), "zoom", "1");
restoreSelect($("tile"), "tile", "480x340");
for (const p of DATA.pages) { const o = document.createElement("option"); o.value = p.id; o.textContent = p.file; $("page").appendChild(o); }
buildMethods();
selectPage(store.get("page", null));
</script>
</body>
</html>
)HTML";
}  // namespace

QString viewerMode(const bool colors) {
  return colors ? QStringLiteral("colors") : QStringLiteral("binarization");
}

bool writeViewer(const QDir& outDir, const bool colors) {
  QJsonArray pages;
  for (const QString& name : outDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    QFile infoFile(outDir.filePath(name + "/info.json"));
    if (infoFile.open(QIODevice::ReadOnly)) {
      // Only pages of this comparison: a run with and one without --colors may share the folder.
      const QJsonObject page = QJsonDocument::fromJson(infoFile.readAll()).object();
      if (page.value("mode").toString() == viewerMode(colors)) {
        pages.append(page);
      }
    }
  }
  const QString dataPath = outDir.filePath("data.js");
  const QString indexPath = outDir.filePath("index.html");
  if ((QFileInfo::exists(dataPath) && !QFileInfo(dataPath).isWritable())
      || (QFileInfo::exists(indexPath) && !QFileInfo(indexPath).isWritable())) {
    return false;
  }

  QByteArray dataBytes("const DATA = ");
  dataBytes.append(
      QJsonDocument(QJsonObject{{"mode", viewerMode(colors)}, {"pages", pages}}).toJson(QJsonDocument::Compact));
  dataBytes.append(";\n");

  const QByteArray htmlBytes(kIndexHtml);

  QSaveFile data(dataPath);
  QSaveFile index(indexPath);
  if (!data.open(QIODevice::WriteOnly) || !index.open(QIODevice::WriteOnly)) {
    return false;
  }
  if ((data.write(dataBytes) != dataBytes.size()) || (index.write(htmlBytes) != htmlBytes.size())) {
    return false;
  }
  return index.commit() && data.commit();
}
}  // namespace binarization_compare
