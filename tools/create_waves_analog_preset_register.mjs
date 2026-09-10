import fs from "node:fs/promises";
import path from "node:path";
import { SpreadsheetFile, Workbook } from "@oai/artifact-tool";

const wavesRoot = "C:\\Program Files (x86)\\Waves\\Data\\Setup Libraries";
const outputDir = "C:\\Users\\amano\\Documents\\Amanorsac Studio\\APPS\\Amanorsac Plugins\\outputs\\waves_analog_preset_register";
const outputFile = path.join(outputDir, "Waves_Analog_Preset_Register.xlsx");

const sources = [
  { folder: "AudioTrack Setups library", plugin: "AudioTrack", apps: "A01 Heritage EQ; A02 Iron Pre; A03 Console One; A06 Strike FET; A08 Busforge VCA", include: () => true, presetInclude: (name) => !/noise|hum|wind|de-ess|band-pass|shockwave|realaudio|multimedia|telephone/i.test(name), function: "Channel-strip starting point" },
  { folder: "C1 Setups Library", plugin: "C1", apps: "A03 Console One; A06 Strike FET; A08 Busforge VCA", include: (file) => /BasicDynamic|Simple Setups|Compression/i.test(file), presetInclude: (name) => /compress/i.test(name), function: "Compression / dynamics starting point" },
  { folder: "Q10 Setups Library", plugin: "Q10", apps: "A01 Heritage EQ", include: (file) => /EQ filters|Hum Removal|Try These First|Tilt setups|Semi-Tilt|Plateau/i.test(file), presetInclude: (name) => !/amradio|speech|telephone|pseudostereo|multimedia|hum|crossover|distortion/i.test(name), function: "EQ curve starting point" },
  { folder: "Renaissance EQ Setup Lib", plugin: "Renaissance EQ", apps: "A01 Heritage EQ", include: () => true, presetInclude: (name) => !/hum|comb|rezonator|hole|octave|steep/i.test(name), function: "Musical EQ curve starting point" },
  { folder: "TrueVerb Setups Library", plugin: "TrueVerb", apps: "A10 Plate Four", include: (file) => /^Plates( \(send\))?\.xps$/i.test(file), presetInclude: () => true, function: "Plate reverb starting point" },
];

const xmlEscape = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
const clean = (s) => s.replace(/\s+/g, " ").trim();

async function walk(dir) {
  const entries = await fs.readdir(dir, { withFileTypes: true });
  const output = [];
  for (const entry of entries) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) output.push(...await walk(full));
    else output.push(full);
  }
  return output;
}

function parseXps(text) {
  const results = [];
  const presetRe = /<Preset\s+GenericType="([^"]*)"\s+Name="([^"]*)">([\s\S]*?)<\/Preset>/g;
  for (const match of text.matchAll(presetRe)) {
    const params = match[3].match(/<Parameters[^>]*>([\s\S]*?)<\/Parameters>/);
    const raw = params ? clean(params[1]) : "";
    results.push({ genericType: match[1], name: match[2], raw, count: raw ? raw.split(" ").length : 0 });
  }
  return results;
}

const allRows = [];
for (const source of sources) {
  const sourceDir = path.join(wavesRoot, source.folder);
  const files = (await walk(sourceDir)).filter((file) => path.extname(file).toLowerCase() === ".xps" && source.include(path.basename(file)));
  for (const file of files) {
    const presets = parseXps(await fs.readFile(file, "utf8"));
    for (const preset of presets.filter((preset) => source.presetInclude(preset.name))) {
      allRows.push([
        source.folder,
        path.basename(file),
        source.plugin,
        preset.name,
        preset.raw,
        preset.count,
        source.apps,
        source.function,
        "",
        "",
        "Needs review",
      ]);
    }
  }
}
allRows.sort((a, b) => a[2].localeCompare(b[2]) || a[3].localeCompare(b[3]));

const wb = Workbook.create();
const overview = wb.worksheets.add("Overview");
const presets = wb.worksheets.add("Preset Register");
for (const sheet of [overview, presets]) { sheet.showGridLines = false; sheet.tabColor = "#1D2933"; }

overview.getRange("A1:G1").merge();
overview.getRange("A1").values = [["Waves Presets Relevant to Amanorsac Analog Apps"]];
overview.getRange("A2:G2").merge();
overview.getRange("A2").values = [["Working register - update the yellow columns after listening and translating a usable preset."]];
overview.getRange("A4:D4").values = [["Source library", "Included preset count", "Relevant Amanorsac app(s)", "Reason included"]];
const summary = sources.map((s) => [s.folder, allRows.filter((r) => r[0] === s.folder).length, s.apps, s.function]);
overview.getRange(`A5:D${4 + summary.length}`).values = summary;
overview.getRange("A12:G12").merge();
overview.getRange("A12").values = [["Excluded as not relevant to the current Analog collection: MetaFlanger and PS22/DLA. C1 noise reduction, de-essing, enhancers and keying examples are also excluded."]];
overview.getRange("A14:G14").merge();
overview.getRange("A14").values = [["Parameter values are preserved exactly as raw Waves state values. They are not yet decoded into named Waves controls, so use them as reference data while manually building Amanorsac presets."]];

const headers = ["Source Library", "Source File", "Waves Plugin", "Original Preset Name", "Raw Parameter Values", "Parameter Count", "Relevant Amanorsac App(s)", "Suggested Function", "Proposed Amanorsac Preset Name", "Proposed Amanorsac App", "Review Status"];
presets.getRange("A1:K1").merge();
presets.getRange("A1").values = [["Analog-Relevant Waves Preset Register"]];
presets.getRange("A2:K2").merge();
presets.getRange("A2").values = [["Only presets relevant to A01-A10 Analog apps are included. Yellow columns are your editable translation fields."]];
presets.getRange("A4:K4").values = [headers];
if (allRows.length) presets.getRange(`A5:K${4 + allRows.length}`).values = allRows;

const titleFmt = { fill: "#1D2933", font: { name: "Arial", size: 16, bold: true, color: "#F3D78B" }, horizontalAlignment: "left", verticalAlignment: "center" };
const subtitleFmt = { font: { name: "Arial", size: 10, italic: true, color: "#55636C" }, verticalAlignment: "center" };
const headFmt = { fill: "#36454F", font: { name: "Arial", size: 10, bold: true, color: "#FFFFFF" }, horizontalAlignment: "center", verticalAlignment: "center", wrapText: true, borders: { preset: "all", style: "thin", color: "#FFFFFF" } };
overview.getRange("A1:G1").format = titleFmt; overview.getRange("A2:G2").format = subtitleFmt; overview.getRange("A4:D4").format = headFmt;
presets.getRange("A1:K1").format = titleFmt; presets.getRange("A2:K2").format = subtitleFmt; presets.getRange("A4:K4").format = headFmt;
overview.getRange("A1:G1").format.rowHeight = 28; presets.getRange("A1:K1").format.rowHeight = 28;
overview.getRange("A2:G2").format.rowHeight = 22; presets.getRange("A2:K2").format.rowHeight = 22;
overview.getRange("A4:D9").format.font = { name: "Arial", size: 10, color: "#24313A" };
overview.getRange("A5:D9").format.wrapText = true;
overview.getRange("A5:D9").format.verticalAlignment = "top";
overview.getRange("A5:D9").format.borders = { preset: "inside", style: "thin", color: "#D8E0E4" };
overview.getRange("A12:G14").format.font = { name: "Arial", size: 10, color: "#4B5961" };
overview.getRange("A12:G14").format.wrapText = true;

const last = 4 + allRows.length;
presets.getRange(`A5:K${last}`).format.font = { name: "Arial", size: 9, color: "#24313A" };
presets.getRange(`A5:K${last}`).format.verticalAlignment = "top";
presets.getRange(`A5:K${last}`).format.wrapText = true;
presets.getRange(`A5:K${last}`).format.borders = { preset: "inside", style: "thin", color: "#D8E0E4" };
presets.getRange(`I5:J${last}`).format.fill = "#FFF2CC";
presets.getRange(`K5:K${last}`).format.fill = "#EAF2F8";
presets.getRange(`F5:F${last}`).format.horizontalAlignment = "center";
presets.getRange(`A4:K${last}`).format.autofitRows();
overview.getRange("A:A").format.columnWidth = 27; overview.getRange("B:B").format.columnWidth = 18; overview.getRange("C:C").format.columnWidth = 46; overview.getRange("D:D").format.columnWidth = 35; overview.getRange("E:G").format.columnWidth = 12;
presets.getRange("A:A").format.columnWidth = 27; presets.getRange("B:B").format.columnWidth = 29; presets.getRange("C:C").format.columnWidth = 16; presets.getRange("D:D").format.columnWidth = 30; presets.getRange("E:E").format.columnWidth = 80; presets.getRange("F:F").format.columnWidth = 14; presets.getRange("G:G").format.columnWidth = 42; presets.getRange("H:H").format.columnWidth = 29; presets.getRange("I:I").format.columnWidth = 34; presets.getRange("J:J").format.columnWidth = 30; presets.getRange("K:K").format.columnWidth = 16;
presets.freezePanes.freezeRows(4); presets.freezePanes.freezeColumns(4);
presets.getRange(`K5:K${last}`).dataValidation = { rule: { type: "list", values: ["Needs review", "Keep", "Translate", "Not suitable", "Complete"] } };
presets.getRange(`J5:J${last}`).dataValidation = { rule: { type: "list", values: ["A01 Heritage EQ", "A02 Iron Pre", "A03 Console One", "A06 Strike FET", "A08 Busforge VCA", "A10 Plate Four"] } };
presets.tables.add(`A4:K${last}`, true, "WavesAnalogPresets");
wb.recalculate();
await fs.mkdir(outputDir, { recursive: true });
const preview = await wb.render({ sheetName: "Preset Register", range: "A1:K24", scale: 1, format: "png" });
await fs.writeFile(path.join(outputDir, "preview.png"), new Uint8Array(await preview.arrayBuffer()));
const check = await wb.inspect({ kind: "table", range: "Preset Register!A1:K12", include: "values,formulas", tableMaxRows: 12, tableMaxCols: 11 });
console.log(check.ndjson);
const xlsx = await SpreadsheetFile.exportXlsx(wb);
await xlsx.save(outputFile);
console.log(`Rows included: ${allRows.length}`);
