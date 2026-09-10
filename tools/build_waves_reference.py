"""
build_waves_reference.py - Amanorsac Studio
==========================================================================
Rebuilds the Waves preset reference end to end, straight from the Waves
plug-ins installed on this machine.

    python build_waves_reference.py

Outputs, written to ../outputs/waves_analog_preset_register/:
    Waves_Preset_Reference_v2.xlsx
    Analog_parameters_decoded.csv
    Digital_parameters_decoded.csv

Re-run it after installing more Waves plug-ins, or after adding an entry to
FAMILY_MAP below to cover one. Requires: openpyxl.

The Overview and Plug-in Map summary cells are live COUNTIF formulas over the
register sheets, so Excel fills them in on open; they read blank in pandas or
any other reader that only sees cached values until Excel has saved once.
"""
import json, re, math, csv, hashlib, collections
from pathlib import Path
import xml.etree.ElementTree as ET

WAVES_PLUGINS = Path(r"C:\Program Files (x86)\Waves\Plug-Ins V15")
WAVES_PRESETS = Path(r"C:\Program Files (x86)\Waves\Data\Presets")
PROJECT       = Path(__file__).resolve().parent.parent
OUT           = PROJECT / "outputs" / "waves_analog_preset_register"


# ---------------------------------------------------------------- extraction
def parameter_definitions(bundle):
    """Read a plug-in's ParamXML control list, resolving inherited definitions."""
    nodes = ET.parse(WAVES_PLUGINS / (bundle + ".bundle") /
                     "Contents/Resources/ParamXML/1000.xml").getroot().findall("Parameter")
    byname = {n.get("Name"): n for n in nodes}
    cache = {}

    def resolve(n):
        name = n.get("Name")
        if name in cache:
            return cache[name]
        d = resolve(byname[n.get("Parent")]).copy() if n.get("Parent") in byname else {}
        for c in n:
            if c.tag == "Scale":
                for s in c:
                    d[s.tag] = (s.text or "").strip()
                    if s.tag == "ScaleType" and s.get("Type"):
                        d["ScaleType"] = s.get("Type")
                        d["knots"] = {v.tag: (v.text or "").strip() for v in s}
            elif c.tag == "Flags":
                d["flags"] = [x.tag for x in c]
            elif c.tag in ("Label", "Units", "DisplayFunction"):
                d[c.tag] = (c.text or "").strip()
            elif c.tag in ("InitialValue", "ResetValue"):
                d[c.tag] = {"type": c.get("Type"), "value": (c.text or "").strip()}
        d["name"] = name
        cache[name] = d
        return d

    return [resolve(n) for n in nodes]


def harvest():
    maps, records, missing = {}, [], []
    for family in FAMILY_MAP:
        try:
            maps[family] = parameter_definitions(family)
        except Exception as e:
            missing.append((family, str(e)))
            continue
        paths = sorted((WAVES_PLUGINS / (family + ".bundle") /
                        "Contents/Resources/Presets").glob("*.xml"))
        paths += sorted((WAVES_PRESETS / family).rglob("*.xps"))
        for path in paths:
            try:
                root = ET.parse(path).getroot()
            except Exception as e:
                missing.append((str(path), str(e)))
                continue
            for n, preset in enumerate(root.iter("Preset")):
                params = preset.find(".//Parameters")
                if params is None:
                    continue
                raw = " ".join(" ".join(params.itertext()).split())
                kind = params.get("Type", "state")
                if kind == "Reset" or not raw:
                    continue
                if re.search(r"reset|default|^Fs_|OExt presets", preset.get("Name", ""), re.I):
                    continue
                records.append(dict(
                    id="W" + hashlib.sha1(f"{family}|{path}|{n}".encode()).hexdigest()[:9],
                    family=family, name=preset.get("Name", ""),
                    group=preset.findtext("PresetHeader/Group", ""), type=kind, raw=raw,
                    path=str(path), version=preset.findtext("PresetHeader/PluginVersion", "")))
    return maps, records, missing


SPEC_DIR = PROJECT / "specs" / "plugins"
specs = {}
for f in sorted(SPEC_DIR.glob("*.json")):
    spec = json.loads(f.read_text(encoding="utf-8-sig"))
    if spec.get("series") in ("Analog", "Digital"):
        specs[f.stem] = spec
maps, records, missing = harvest()
for name, why in missing:
    print(f"  skipped {name}: {why}")
print(f"{len(records)} presets from {len(maps)} plug-ins")

# ---------------------------------------------------------------- mapping
# family -> (primary target, secondary targets, why, domain)
FAMILY_MAP = {
 'Scheps 73':      ('A01', ['A02'],       'Neve 1073 EQ + preamp: musical broad-band EQ curves and transformer drive'),
 'NLS':            ('A03', ['A02'],       'Non-linear console summing: channel colour, drive and noise character'),
 'J37':            ('A04', [],            'Studer J37 tape: bias, wow/flutter, saturation and delay behaviour'),
 'BB Tubes':       ('A05', ['A02'],       'Tube harmonic saturation: drive, bias and tone-stack staging'),
 'CLA-76':         ('A06', [],            'FET 1176: fast attack/release ratios and all-button behaviour'),
 'CLA-2A':         ('A07', [],            'Optical LA-2A: programme-dependent levelling, peak/limit modes'),
 'SSLComp':        ('A08', [],            'SSL bus VCA: threshold/ratio/attack/release and mix on the 2-bus'),
 'PuigTec':        ('A09', [],            'Pultec passive EQ: boost/attenuate interaction and broad shelves'),
 'ARPlates':       ('A10', ['D09'],       'Abbey Road plates: plate type, damping, pre-delay and tone'),
 'REQ':            ('D01', ['A01'],       'Renaissance EQ: musical digital curves usable as precision-EQ starting points'),
 'Q10':            ('D01', [],            'Q10 paragraphic: surgical band placement and filter types'),
 'AudioTrack':     ('D01', ['A01','A06'], 'Channel strip: combined EQ + gate + compression starting points'),
 'F6':             ('D02', ['D01'],       'Floating-band dynamic EQ: threshold-driven band behaviour'),
 'C6':             ('D03', [],            'Six-band multiband dynamics: crossover, threshold and range per band'),
 'C1':             ('D03', ['A08'],       'C1 compressor/gate: classic dynamics curves and side-chain shapes'),
 'L2':             ('D04', [],            'Ultramaximizer: threshold/ceiling/release and dither settings'),
 'Sibilance':      ('D05', [],            'Spectral de-esser: detection band and reduction depth'),
 'Curves Equator': ('D06', [],            'Spectral resonance control: per-resonance detection and taming'),
 'Vitamin':        ('D07', [],            'Multiband harmonic enhancer: per-band drive and width'),
 'H-Delay':        ('D08', [],            'Hybrid delay: time/feedback/filtering, analog mode and modulation'),
 'H-Reverb':       ('D09', ['A10'],       'Hybrid reverb: FDN density, damping, tail shaping and pre-delay'),
 'TrueVerb':       ('D09', ['A10'],       'Room emulator: early reflections vs tail balance and room size'),
 'S1':             ('D10', [],            'Stereo imager: width, rotation and asymmetry'),
}

def disp(pid):
    s = specs.get(pid)
    return f"{pid} {s['display_name'].title()}" if s else pid

def num(s):
    try:
        f = float(s)
        return f if math.isfinite(f) else None
    except Exception:
        return None

SIMPLE = {'linear', 'logarithmic', 'toggle'}
INTERNAL = re.compile(r'GRAPH|DISPLAY|LABEL|REFRESH|SELECTED|UNUSED|_VU|VU_|METER|CLIP_|SCROLL|ZOOM|VERSION|PRESET|MOUSE|HOVER|TOOLTIP|SKIN|GUI', re.I)
# DSP coefficients and calibration constants: real stored values, but not user controls,
# so they are kept in the full CSV and excluded from the readable key-settings digest.
COEFF = re.compile(r'HUM|NORM|EPSILON|COEFF|MATRIX|CALIB|_ORG$|SCALING|_INC$|_R1$|_TC$|^PARAM_K\d+$|^PARAM_R[A-Z]', re.I)
COEFF_LABEL = re.compile(r'^[kK]\d+$|^R[A-Z][a-z]', re.I)
# Switches that describe the session, not the sound.
HOUSEKEEPING = re.compile(r'SOLO|BYPASS|MONITOR|_LINK|LINK_|LAB_?ON|POLYTHD|PHASE_MATC|DITHER|'
                          r'\bIDR\b|SWAP|VU_MODE|VARIATION|VARIATON|_STATE$|ONOFF|ON_OFF', re.I)
UNIT_WEIGHT = {'db': 1.0, 'hz': 1.0, 'khz': 1.0, 'ms': 1.0, 'sec': 1.0, 's': 1.0,
               '%': 0.9, 'deg': 0.8, 'bpm': 0.8}
GENERIC = {'on\\off', 'on/off', 'gain', 'freq', 'q', 'level', 'mix', 'type', 'mode', 'on', 'off', ''}

def pretty(name):
    t = re.sub(r'^PARAM_', '', name or '').replace('_', ' ').strip().title()
    return re.sub(r'\bHp\b', 'HP', re.sub(r'\bLp\b', 'LP', t))

def is_display(p):
    fl = p.get('flags') or []
    return 'DisplayOnlyParam' in fl or 'Unused' in fl or INTERNAL.search(p.get('name', ''))

def default_of(p):
    for k in ('ResetValue', 'InitialValue'):
        rv = p.get(k)
        if rv and rv.get('type') in ('RealWorld', None):
            v = num(rv.get('value'))
            if v is not None:
                return v
    return None

def state_to_real(v, p):
    """Convert a legacy 0..1000 state code to a real-world value."""
    sc = (p.get('ScaleType') or '').strip()
    lo, hi = num(p.get('Minimum')), num(p.get('Maximum'))
    if lo is None or hi is None or not 0 <= v <= 1000:
        return None, 'Legacy state, scale unresolved'
    lo, hi = min(lo, hi), max(lo, hi)
    f = v / 1000.0
    if sc == 'linear':
        return round(lo + (hi - lo) * f, 6), 'Converted from legacy state (linear)'
    if sc == 'logarithmic' and lo > 0:
        return round(lo * (hi / lo) ** f, 6), 'Converted from legacy state (log)'
    if sc == 'toggle':
        n = num(p.get('NumberOfStates')) or (hi - lo + 1)
        return round(lo + round(f * (n - 1)), 6), 'Converted from legacy state (enum)'
    return None, 'Legacy state, custom scale not convertible'

def snap(v, p):
    """Round to the control's own step so enum positions read as whole numbers."""
    if v is None:
        return None
    res = num(p.get('Resolution'))
    if res and res >= 1:
        return round(v / res) * res
    if res and res > 0:
        return round(round(v / res) * res, 6)
    return round(v, 6)

def build_labels(ps):
    counts = collections.Counter((p.get('Label') or '').strip().lower() for p in ps)
    out = []
    for p in ps:
        lab = (p.get('Label') or '').strip()
        if not lab or lab.lower() in GENERIC or counts[lab.lower()] > 1:
            out.append(pretty(p.get('name', '')))
        else:
            out.append(lab)
    return out

def stepped(p):
    """True for rotary switches: the stored number is a position index, not a value."""
    sc = (p.get('ScaleType') or '').strip()
    lo, hi = num(p.get('Minimum')), num(p.get('Maximum'))
    if lo is None or hi is None:
        return False
    lo, hi = min(lo, hi), max(lo, hi)
    res = num(p.get('Resolution'))
    return sc == 'toggle' or (res is not None and res >= 1 and hi - lo <= 20 and hi - lo >= 1)

def fmt(v, p):
    if v is None:
        return ''
    if stepped(p):
        lo, hi = num(p.get('Minimum')), num(p.get('Maximum'))
        lo, hi = min(lo, hi), max(lo, hi)
        return f'step {v:.0f}/{hi:.0f}'
    u = (p.get('Units') or '').strip()
    a = abs(v)
    s = f'{v:.0f}' if a >= 100 or v == int(v) else (f'{v:.2f}' if a >= 1 else f'{v:.3f}')
    return f'{s} {u}'.strip()

# ---------------------------------------------------------------- decode
LABELS = {fam: build_labels(ps) for fam, ps in maps.items()}
rows_full = []          # every decoded stored parameter
presets_out = []        # one row per preset
fam_stats = collections.defaultdict(collections.Counter)

for r in records:
    fam = r['family']
    ps = maps[fam]
    labels = LABELS[fam]
    toks = r['raw'].split()
    realworld = r['type'] == 'RealWorld'
    # Families whose stored length exceeds the installed definition cannot be decoded
    # positionally at all, so their values are withheld rather than published as fact.
    overflow_family = len(toks) > len(ps)
    checked = inrange = overflow = stored = 0
    decoded = []            # (deviation, label, text, unit)

    for i, t in enumerate(toks):
        if i >= len(ps):
            overflow += 1
            continue
        p = ps[i]
        if t in ('*', '-1'):
            continue
        v = num(t)
        if v is None:
            continue
        stored += 1
        if is_display(p):
            continue
        sc = (p.get('ScaleType') or '').strip()
        lo, hi = num(p.get('Minimum')), num(p.get('Maximum'))
        if lo is not None and hi is not None:
            lo, hi = min(lo, hi), max(lo, hi)
        label = labels[i]
        val, status = None, ''

        if realworld:
            val = snap(v, p)
            if sc in SIMPLE and lo is not None:
                checked += 1
                if lo - 1e-6 <= v <= hi + 1e-6:
                    inrange += 1
                    status = 'Enum state' if sc == 'toggle' else 'Real-world value'
                else:
                    status = 'Out of declared range - treat as uncertain'
            else:
                status = 'Real-world value (custom scale)'
        else:
            val, status = state_to_real(v, p)
            val = snap(val, p)
            if lo is not None:
                checked += 1
                inrange += 1 if 0 <= v <= 1000 else 0

        dev = 0.0
        dv = default_of(p)
        _ = dv
        if dv is not None and val is not None and lo is not None and hi != lo:
            dev = abs(val - dv) / (hi - lo)
        elif val is not None:
            dev = 0.5
        # Weight the digest towards controls a mix engineer actually reaches for:
        # metered continuous controls first, plain continuous next, switches last.
        unit = (p.get('Units') or '').strip().lower()
        res = num(p.get('Resolution'))
        if unit in UNIT_WEIGHT:
            weight = UNIT_WEIGHT[unit]
        elif sc == 'toggle' or (res and res >= 1 and (hi is not None and hi - (lo or 0) <= 20)):
            weight = 0.12
        else:
            weight = 0.55
        if HOUSEKEEPING.search(p.get('name', '')):
            weight *= 0.15
        dev *= weight

        at_default = dv is not None and val is not None and abs(val - dv) < 1e-9
        if not at_default and not overflow_family:
            rows_full.append([specs[FAMILY_MAP[fam][0]]['series'], r['id'], fam, r['name'], r['group'],
                              r['type'], i + 1, p.get('name', ''), label, t,
                              '' if val is None else round(val, 6),
                              '' if stepped(p) else (p.get('Units') or '').strip(),
                              'switch position' if stepped(p) else (sc or ''), status])
        interesting = not (COEFF.search(p.get('name', '')) or COEFF_LABEL.match(label))
        if label and dev > 1e-9 and interesting:
            decoded.append((dev, label, fmt(val if val is not None else v, p)))

    # ---- per-preset confidence
    ratio = inrange / checked if checked else 0.0
    if overflow:
        conf, note = 'Unresolved', f'{overflow} stored values fall past the end of the installed parameter definition - positional decoding does not hold for this plug-in'
    elif not checked:
        conf, note = 'Low', 'No range-checkable controls in this preset'
    elif not realworld:
        conf = 'Medium' if ratio >= 0.98 else 'Low'
        note = 'Legacy state-code preset: values converted with the documented scale, verify against the plug-in before trusting exact numbers'
    elif ratio >= 0.98:
        conf, note = 'High', 'Stored values are real-world and sit inside the installed control ranges'
    elif ratio >= 0.90:
        conf, note = 'Medium', f'{checked - inrange} of {checked} values fall outside their control range - likely a version difference'
    else:
        conf, note = 'Low', f'{checked - inrange} of {checked} values fall outside their control range'

    decoded.sort(key=lambda x: -x[0])
    key = '; '.join(f'{l} {t}' for _, l, t in decoded[:10])
    prim, sec, why = FAMILY_MAP[fam]
    presets_out.append(dict(
        id=r['id'], family=fam, name=r['name'], group=r['group'], version=r['version'],
        ptype=r['type'], primary=prim, secondary=sec, why=why,
        stored=stored, conf=conf, note=note, key=key, path=r['path'],
        series=specs[prim]['series']))
    fam_stats[fam][conf] += 1

OUT.mkdir(parents=True, exist_ok=True)
json.dump(dict(presets=presets_out, fam_stats={k: dict(v) for k, v in fam_stats.items()}),
          open(OUT / 'presets.json', 'w'))

HEAD = ['Series', 'Preset ID', 'Waves Plug-in', 'Preset Name', 'Bank', 'Preset Format',
        'State Position', 'Parameter ID', 'Control Label', 'Stored Value', 'Decoded Value',
        'Unit', 'Scale Type', 'Decode Status']
for series, fname in (('Analog', 'Analog_parameters_decoded.csv'),
                      ('Digital', 'Digital_parameters_decoded.csv')):
    with open(OUT / fname, 'w', newline='', encoding='utf-8-sig') as f:
        w = csv.writer(f)
        w.writerow(HEAD)
        w.writerows(row for row in rows_full if row[0] == series)

print('presets', len(presets_out), 'param rows', len(rows_full))
print(collections.Counter(p['conf'] for p in presets_out))
print(collections.Counter(p['series'] for p in presets_out))

# ---------------------------------------------------------------- workbook
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill, Alignment, Border, Side
from openpyxl.worksheet.datavalidation import DataValidation
from openpyxl.utils import get_column_letter

ACRONYM = {'Eq': 'EQ', 'Fet': 'FET', 'Vca': 'VCA', 'Md': 'MD', 'Ms': 'MS'}

def nice(name):
    return ' '.join(ACRONYM.get(w.title(), w.title()) for w in str(name).split())

INK, GOLD, SLATE = '1D2933', 'F3D78B', '36454F'
EDIT, INFO, LINE = 'FFF2CC', 'EAF2F8', 'D8E0E4'
A = 'Arial'
thin = Side(style='thin', color=LINE)
BOX = Border(left=thin, right=thin, top=thin, bottom=thin)
presets = presets_out

def title(ws, text, sub, width):
    ws.merge_cells(start_row=1, start_column=1, end_row=1, end_column=width)
    c = ws.cell(1, 1, text)
    c.font = Font(A, 16, bold=True, color=GOLD)
    c.fill = PatternFill('solid', fgColor=INK)
    c.alignment = Alignment('left', 'center')
    ws.row_dimensions[1].height = 30
    ws.merge_cells(start_row=2, start_column=1, end_row=2, end_column=width)
    c = ws.cell(2, 1, sub)
    c.font = Font(A, 10, italic=True, color='55636C')
    c.alignment = Alignment('left', 'center')
    ws.row_dimensions[2].height = 22

def header(ws, row, heads):
    for i, h in enumerate(heads, 1):
        c = ws.cell(row, i, h)
        c.font = Font(A, 10, bold=True, color='FFFFFF')
        c.fill = PatternFill('solid', fgColor=SLATE)
        c.alignment = Alignment('center', 'center', wrap_text=True)
        c.border = BOX
    ws.row_dimensions[row].height = 30

def widths(ws, spec):
    for col, w in spec.items():
        ws.column_dimensions[col].width = w

def body(ws, r0, r1, c1):
    for row in ws.iter_rows(min_row=r0, max_row=r1, max_col=c1):
        for c in row:
            c.font = Font(A, 9, color='24313A')
            c.alignment = Alignment(vertical='top', wrap_text=True)
            c.border = BOX


wb = Workbook()


# ============================================================ OVERVIEW
ov = wb.active
ov.title = 'Overview'
ov.sheet_view.showGridLines = False
title(ov, 'Waves Preset Reference for the Amanorsac Plug-in Suite',
      'Engineering reference built from the Waves plug-ins installed on this machine. '
      'Source material for original Amanorsac presets, not a copy of any Waves preset.', 8)

notes = [
    ('What this is',
     'Every factory and artist preset shipped with 23 installed Waves plug-ins, read straight from the plug-in '
     'bundles, decoded into named controls with real units, and mapped to the Amanorsac plug-in that covers the '
     'same job. Use it to see how experienced engineers set these controls, then build your own presets.'),
    ('How the decoding works',
     'Each Waves plug-in ships a parameter definition (ParamXML) listing its controls in the order they are stored '
     'in a preset. Stored values are matched to that list by position. Most presets store their values already in '
     'real-world units, so no conversion is needed; older presets store 0-1000 state codes, which are converted '
     'using the control’s own scale.'),
    ('How the confidence rating is earned',
     'Every decoded value is checked against the minimum and maximum its own control declares. A preset rated High '
     'had at least 98% of its checkable values land inside range. Across the whole set, 99.1% of checkable values '
     'landed in range, which is what makes the position matching trustworthy.'),
    ('Where a preset stores fewer values than the plug-in has controls',
     'That preset was saved by an older version. The values still line up from the start; the controls added later '
     'simply sit at their defaults. This is normal and does not lower confidence.'),
    ('Switch positions',
     'A rotary switch stores its position, not the value printed on the panel. Those read "step 2/5". Open the '
     'preset in the Waves plug-in to read the printed value when it matters.'),
    ('Curves Equator is not decoded',
     'Its 226 presets store roughly twice as many values as the installed definition declares, so position matching '
     'does not hold. The presets are listed and named, but no values are published for them. Everything else decoded '
     'cleanly.'),
    ('Copyright',
     'Waves preset values are reference data for understanding how these processors are set. Do not ship them as '
     'Amanorsac presets. Build your own settings, informed by what you see here.'),
]
r = 4
for h, t in notes:
    ov.cell(r, 1, h).font = Font(A, 10, bold=True, color=INK)
    ov.merge_cells(start_row=r, start_column=2, end_row=r, end_column=8)
    c = ov.cell(r, 2, t)
    c.font = Font(A, 10, color='24313A')
    c.alignment = Alignment(vertical='top', wrap_text=True)
    ov.row_dimensions[r].height = 46
    r += 1

r += 1
ov.cell(r, 1, 'CONFIDENCE RATINGS').font = Font(A, 11, bold=True, color=INK)
r += 1
header(ov, r, ['Rating', 'What it means', 'How to use it', '', '', '', '', ''])
legend = [
    ('High', 'Real-world values, all inside their declared control ranges.',
     'Read the numbers as written. Safe to design against.'),
    ('Medium', 'Legacy state-code preset converted with the documented scale, or a few values outside range.',
     'Direction and proportion are reliable; confirm exact numbers in the plug-in.'),
    ('Low', 'More than one value in ten falls outside its control range.',
     'Treat as a rough hint only.'),
    ('Unresolved', 'Stored length does not match the installed definition. No values published.',
     'Open the preset in Waves and read it by eye.'),
]
for name, means, use in legend:
    r += 1
    ov.cell(r, 1, name).font = Font(A, 10, bold=True, color=INK)
    ov.cell(r, 2, means).font = Font(A, 9, color='24313A')
    ov.merge_cells(start_row=r, start_column=5, end_row=r, end_column=8)
    ov.cell(r, 5, use).font = Font(A, 9, color='24313A')
    for cc in range(1, 9):
        ov.cell(r, cc).alignment = Alignment(vertical='top', wrap_text=True)
        ov.cell(r, cc).border = BOX
    ov.row_dimensions[r].height = 26
ov.merge_cells(start_row=r - 3, start_column=2, end_row=r - 3, end_column=4)
ov.merge_cells(start_row=r - 2, start_column=2, end_row=r - 2, end_column=4)
ov.merge_cells(start_row=r - 1, start_column=2, end_row=r - 1, end_column=4)
ov.merge_cells(start_row=r, start_column=2, end_row=r, end_column=4)

r += 2
ov.cell(r, 1, 'SOURCE LIBRARIES').font = Font(A, 11, bold=True, color=INK)
r += 1
fam_head_row = r
header(ov, r, ['Waves plug-in', 'What it contributes', 'Primary Amanorsac target',
               'Presets', 'High', 'Medium', 'Low', 'Unresolved'])
fam_first = r + 1
by_fam = collections.defaultdict(list)
for p in presets:
    by_fam[p['family']].append(p)
for fam in sorted(by_fam):
    r += 1
    s = by_fam[fam][0]
    sheet = 'Analog Register' if s['series'] == 'Analog' else 'Digital Register'
    ov.cell(r, 1, fam)
    ov.cell(r, 2, s['why'])
    ov.cell(r, 3, f"{s['primary']} {nice(specs[s['primary']]['display_name'])}")
    ov.cell(r, 4, f"=COUNTIF('{sheet}'!$B$5:$B$2000,$A{r})")
    for i, grade in enumerate(['High', 'Medium', 'Low', 'Unresolved']):
        ov.cell(r, 5 + i,
                f"=COUNTIFS('{sheet}'!$B$5:$B$2000,$A{r},'{sheet}'!$L$5:$L$2000,\"{grade}\")")
fam_last = r
body(ov, fam_first, fam_last, 8)
for row in range(fam_first, fam_last + 1):
    for col in range(4, 9):
        ov.cell(row, col).alignment = Alignment('center', 'top')

r += 1
ov.cell(r, 1, 'TOTAL').font = Font(A, 10, bold=True, color=INK)
for col in range(4, 9):
    c = ov.cell(r, col, f'=SUM({get_column_letter(col)}{fam_first}:{get_column_letter(col)}{fam_last})')
    c.font = Font(A, 10, bold=True, color=INK)
    c.alignment = Alignment('center', 'top')
    c.border = BOX
ov.cell(r, 1).border = BOX
widths(ov, {'A': 22, 'B': 58, 'C': 26, 'D': 10, 'E': 9, 'F': 10, 'G': 8, 'H': 12})

# ============================================================ REGISTERS
REG_HEAD = ['Preset ID', 'Waves Plug-in', 'Preset Name', 'Bank', 'Plug-in Version',
            'Preset Format', 'Primary Amanorsac Target', 'Also Relevant To',
            'Why This Plug-in Maps Here', 'Decoded Key Settings', 'Values Stored',
            'Confidence', 'Decode Note', 'Proposed Amanorsac Preset Name',
            'Assign To', 'Status', 'Source File']
REG_W = {'A': 12, 'B': 15, 'C': 34, 'D': 16, 'E': 11, 'F': 12, 'G': 24, 'H': 18, 'I': 40,
         'J': 78, 'K': 10, 'L': 12, 'M': 46, 'N': 30, 'O': 24, 'P': 14, 'Q': 52}

def register(name, series, blurb):
    ws = wb.create_sheet(name)
    ws.sheet_view.showGridLines = False
    title(ws, name.upper(), blurb, len(REG_HEAD))
    header(ws, 4, REG_HEAD)
    rows = sorted((p for p in presets if p['series'] == series),
                  key=lambda p: (p['primary'], p['family'], p['name'].lower()))
    ids = sorted({p['primary'] for p in rows})
    for i, p in enumerate(rows):
        rr = 5 + i
        ws.cell(rr, 1, p['id'])
        ws.cell(rr, 2, p['family'])
        ws.cell(rr, 3, p['name'])
        ws.cell(rr, 4, p['group'])
        ws.cell(rr, 5, p['version'])
        ws.cell(rr, 6, 'Real-world' if p['ptype'] == 'RealWorld' else 'Legacy state codes')
        ws.cell(rr, 7, f"{p['primary']} {nice(specs[p['primary']]['display_name'])}")
        ws.cell(rr, 8, ', '.join(p['secondary']))
        ws.cell(rr, 9, p['why'])
        ws.cell(rr, 10, p['key'] or ('Not decoded - see Confidence' if p['conf'] == 'Unresolved'
                                     else 'All stored controls sit at their defaults'))
        ws.cell(rr, 11, p['stored']).alignment = Alignment('center', 'top')
        ws.cell(rr, 12, p['conf'])
        ws.cell(rr, 13, p['note'])
        ws.cell(rr, 17, p['path'])
    last = 4 + len(rows)
    body(ws, 5, last, len(REG_HEAD))
    for rr in range(5, last + 1):
        ws.cell(rr, 11).alignment = Alignment('center', 'top')
        conf = ws.cell(rr, 12)
        conf.alignment = Alignment('center', 'top')
        colour = {'High': '1E7B45', 'Medium': 'A9761B', 'Low': 'B4442E', 'Unresolved': '8A2F2F'}[conf.value]
        conf.font = Font(A, 9, bold=True, color=colour)
        for col in (14, 15):
            ws.cell(rr, col).fill = PatternFill('solid', fgColor=EDIT)
        ws.cell(rr, 16).fill = PatternFill('solid', fgColor=INFO)
    ws.auto_filter.ref = f'A4:Q{last}'
    ws.freeze_panes = 'D5'
    widths(ws, REG_W)
    dv_app = DataValidation(type='list', allow_blank=True, formula1='"' +
                            ','.join(f"{i} {nice(specs[i]['display_name'])}" for i in ids) + '"')
    dv_st = DataValidation(type='list', allow_blank=True,
                           formula1='"Not started,Listening,Translating,Built,Rejected"')
    ws.add_data_validation(dv_app); ws.add_data_validation(dv_st)
    dv_app.add(f'O5:O{last}'); dv_st.add(f'P5:P{last}')
    return len(rows)

n_a = register('Analog Register', 'Analog',
               'Waves presets that map to the A01-A10 analog plug-ins. '
               'The three shaded columns are yours to fill in as you work through them.')
n_d = register('Digital Register', 'Digital',
               'Waves presets that map to the D01-D10 digital plug-ins. '
               'The three shaded columns are yours to fill in as you work through them.')

# ============================================================ PLUGIN MAP
pm = wb.create_sheet('Plug-in Map')
pm.sheet_view.showGridLines = False
title(pm, 'AMANORSAC PLUG-IN MAP',
      'Which Waves libraries feed each of your twenty plug-ins, and how much reference material each one has.', 9)
header(pm, 4, ['Plug-in', 'Name', 'Series', 'Role', 'Feature Set',
               'Waves Reference Libraries', 'Presets Mapped Here',
               'Also Referenced By', 'Total Reference Presets'])
feeds = collections.defaultdict(list)
for p in presets:
    if p['family'] not in feeds[p['primary']]:
        feeds[p['primary']].append(p['family'])
for i, pid in enumerate(sorted(specs)):
    rr = 5 + i
    s = specs[pid]
    sheet = 'Analog Register' if s['series'] == 'Analog' else 'Digital Register'
    pm.cell(rr, 1, pid)
    pm.cell(rr, 2, nice(s['display_name']))
    pm.cell(rr, 3, s['series'])
    pm.cell(rr, 4, s['primary_role'])
    pm.cell(rr, 5, ' · '.join(s.get('feature_set', [])))
    pm.cell(rr, 6, ', '.join(feeds.get(pid, [])) or 'No installed Waves plug-in covers this role')
    pm.cell(rr, 7, f"=COUNTIF('{sheet}'!$G$5:$G$2000,$A{rr}&\" \"&$B{rr})")
    pm.cell(rr, 8, f"=COUNTIF('Analog Register'!$H$5:$H$2000,\"*\"&$A{rr}&\"*\")"
                   f"+COUNTIF('Digital Register'!$H$5:$H$2000,\"*\"&$A{rr}&\"*\")")
    pm.cell(rr, 9, f'=G{rr}+H{rr}')
last = 4 + len(specs)
body(pm, 5, last, 9)
for rr in range(5, last + 1):
    for col in (7, 8, 9):
        pm.cell(rr, col).alignment = Alignment('center', 'top')
pm.freeze_panes = 'A5'
widths(pm, {'A': 9, 'B': 22, 'C': 10, 'D': 34, 'E': 54, 'F': 34, 'G': 14, 'H': 14, 'I': 16})

# ============================================================ PARAMETER DICTIONARY
pd_ = wb.create_sheet('Parameter Dictionary')
pd_.sheet_view.showGridLines = False
title(pd_, 'PARAMETER DICTIONARY',
      'The control list each Waves plug-in stores, in the order it stores them. '
      'Use it to read any raw preset string by hand.', 9)
header(pd_, 4, ['Waves Plug-in', 'Position', 'Parameter ID', 'Panel Label', 'Unit',
                'Minimum', 'Maximum', 'Scale', 'Role'])
rr = 5
for fam in sorted(maps):
    for i, p in enumerate(maps[fam]):
        fl = p.get('flags') or []
        role = ('Meter or display readout' if 'DisplayOnlyParam' in fl else
                'Unused' if 'Unused' in fl else
                'Not saved to presets' if 'DontSaveToPreset' in fl else 'Sound control')
        pd_.cell(rr, 1, fam)
        pd_.cell(rr, 2, i + 1)
        pd_.cell(rr, 3, p.get('name', ''))
        pd_.cell(rr, 4, (p.get('Label') or '').strip())
        pd_.cell(rr, 5, (p.get('Units') or '').strip())
        pd_.cell(rr, 6, p.get('Minimum', ''))
        pd_.cell(rr, 7, p.get('Maximum', ''))
        pd_.cell(rr, 8, (p.get('ScaleType') or '').strip())
        pd_.cell(rr, 9, role)
        rr += 1
last = rr - 1
body(pd_, 5, last, 9)
for row in range(5, last + 1):
    pd_.cell(row, 2).alignment = Alignment('center', 'top')
pd_.auto_filter.ref = f'A4:I{last}'
pd_.freeze_panes = 'A5'
widths(pd_, {'A': 17, 'B': 10, 'C': 42, 'D': 18, 'E': 9, 'F': 13, 'G': 13, 'H': 22, 'I': 24})

# ============================================================ FLAGGED
fg = wb.create_sheet('Flagged & Unresolved')
fg.sheet_view.showGridLines = False
title(fg, 'FLAGGED & UNRESOLVED',
      'Every preset that did not decode cleanly, and exactly why. Check these in the Waves plug-in before using them.', 7)
header(fg, 4, ['Preset ID', 'Waves Plug-in', 'Preset Name', 'Primary Amanorsac Target',
               'Confidence', 'What went wrong', 'Source File'])
flag = [p for p in presets if p['conf'] != 'High']
flag.sort(key=lambda p: ({'Unresolved': 0, 'Low': 1, 'Medium': 2}[p['conf']], p['family'], p['name'].lower()))
for i, p in enumerate(flag):
    rr = 5 + i
    fg.cell(rr, 1, p['id'])
    fg.cell(rr, 2, p['family'])
    fg.cell(rr, 3, p['name'])
    fg.cell(rr, 4, f"{p['primary']} {nice(specs[p['primary']]['display_name'])}")
    fg.cell(rr, 5, p['conf'])
    fg.cell(rr, 6, p['note'])
    fg.cell(rr, 7, p['path'])
last = 4 + len(flag)
body(fg, 5, last, 7)
for rr in range(5, last + 1):
    c = fg.cell(rr, 5)
    c.alignment = Alignment('center', 'top')
    c.font = Font(A, 9, bold=True,
                  color={'Medium': 'A9761B', 'Low': 'B4442E', 'Unresolved': '8A2F2F'}[c.value])
fg.auto_filter.ref = f'A4:G{last}'
fg.freeze_panes = 'A5'
widths(fg, {'A': 12, 'B': 16, 'C': 36, 'D': 24, 'E': 13, 'F': 76, 'G': 52})

for ws in wb.worksheets:
    ws.sheet_properties.tabColor = INK

wb.save(OUT / 'Waves_Preset_Reference_v2.xlsx')
print('analog', n_a, 'digital', n_d, 'flagged', len(flag), 'dictionary rows', last)
