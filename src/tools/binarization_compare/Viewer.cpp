// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Viewer.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace binarization_compare {
namespace {
// The page is in German, as for the person comparing the pages.
const char* const kIndexHtml = R"HTML(<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Binarisierung im Vergleich</title>
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
  <h1>Binarisierung im Vergleich</h1>
  <label>Seite <select id="page"></select></label>
  <label>Zoom <select id="zoom">
    <option value="0.5">50 %</option><option value="1" selected>100 %</option>
    <option value="2">200 %</option><option value="3">300 %</option></select></label>
  <label>Kachel <select id="tile">
    <option value="360x260">klein</option><option value="480x340" selected>mittel</option>
    <option value="640x460">groß</option></select></label>
  <span class="hint">Übersicht anklicken oder Pfeiltasten: Ausschnitt verschieben. Kachel anklicken: ganzes Bild.</span>
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
const GROUPS = { original: ["Original (Graustufen)", "#888"], ours: ["ScanTailor OCR", "var(--ours)"],
  experimental: ["ScanTailor Experimental", "var(--experimental)"], filter: ["Filter (Graustufen)", "var(--filter)"] };
const store = {
  get(k, d) { try { const v = localStorage.getItem("bc_" + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } },
  set(k, v) { try { localStorage.setItem("bc_" + k, JSON.stringify(v)); } catch (e) {} }
};
const $ = (id) => document.getElementById(id);
let page = null, cx = 0, cy = 0;
const hidden = new Set(store.get("hidden", []));

function results(p) {
  return [{ id: "original", label: "Original", group: "original", parameters: "", ms: null, file: "original.jpg" }]
    .concat(p.results);
}
function tileSize() { return $("tile").value.split("x").map(Number); }
function zoom() { return Number($("zoom").value); }

function buildMethods() {
  const box = $("methods"); box.innerHTML = "";
  let group = null;
  for (const r of results(DATA.pages[0] || { results: [] })) {
    if (r.group !== group) {
      group = r.group;
      const h = document.createElement("h3"); h.textContent = GROUPS[group][0]; box.appendChild(h);
    }
    const l = document.createElement("label");
    const c = document.createElement("input"); c.type = "checkbox"; c.checked = !hidden.has(r.id);
    c.onchange = () => { c.checked ? hidden.delete(r.id) : hidden.add(r.id); store.set("hidden", [...hidden]); render(); };
    l.append(c, " " + r.label); box.appendChild(l);
  }
}

function selectPage(id) {
  page = DATA.pages.find((p) => p.id === id) || DATA.pages[0];
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
    const t = document.createElement("div"); t.className = "tile"; t.style.setProperty("--g", GROUPS[r.group][1]);
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
  const [, th] = tileSize(), z = zoom();
  const views = document.querySelectorAll(".view");
  const tw = views.length ? views[0].clientWidth : tileSize()[0];
  const w = tw / z, h = th / z;
  cx = Math.min(Math.max(cx, w / 2), Math.max(page.width - w / 2, w / 2));
  cy = Math.min(Math.max(cy, h / 2), Math.max(page.height - h / 2, h / 2));
  for (const v of views) v.style.backgroundPosition = -(cx - w / 2) * z + "px " + -(cy - h / 2) * z + "px";
  const ov = $("ovimg"), f = $("frame"), s = ov.clientWidth / page.width;
  f.style.left = (cx - w / 2) * s + "px"; f.style.top = (cy - h / 2) * s + "px";
  f.style.width = w * s + "px"; f.style.height = h * s + "px";
}

$("overview").onclick = (e) => {
  const rect = $("ovimg").getBoundingClientRect(), s = rect.width / page.width;
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

$("zoom").value = store.get("zoom", "1");
$("tile").value = store.get("tile", "480x340");
for (const p of DATA.pages) { const o = document.createElement("option"); o.value = p.id; o.textContent = p.file; $("page").appendChild(o); }
buildMethods();
selectPage(store.get("page", null));
</script>
</body>
</html>
)HTML";
}  // namespace

bool writeViewer(const QDir& outDir, const bool colors) {
  QJsonArray pages;
  for (const QString& name : outDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    QFile infoFile(outDir.filePath(name + "/info.json"));
    if (infoFile.open(QIODevice::ReadOnly)) {
      pages.append(QJsonDocument::fromJson(infoFile.readAll()).object());
    }
  }
  QFile data(outDir.filePath("data.js"));
  QFile index(outDir.filePath("index.html"));
  if (!data.open(QIODevice::WriteOnly | QIODevice::Truncate)
      || !index.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  data.write("const DATA = ");
  data.write(QJsonDocument(QJsonObject{{"pages", pages}}).toJson(QJsonDocument::Compact));
  data.write(";\n");
  QString html = QString::fromUtf8(kIndexHtml);
  if (colors) {
    html.replace(QStringLiteral("Binarisierung im Vergleich"), QStringLiteral("Farbiger Text im Vergleich"));
    html.replace(QStringLiteral("Original (Graustufen)"), QStringLiteral("Original (Farbe)"));
    html.replace(QStringLiteral("\"ScanTailor Experimental\""),
                 QStringLiteral("\"K-Means (eigener Code, Idee aus ScanTailor Experimental)\""));
    html.replace(QStringLiteral("Filter (Graustufen)"),
                 QStringLiteral("K-Means (Originalcode ScanTailor Experimental)"));
  }
  index.write(html.toUtf8());
  return true;
}
}  // namespace binarization_compare
