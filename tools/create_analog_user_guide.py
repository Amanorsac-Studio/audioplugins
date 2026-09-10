from pathlib import Path
import json
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT, TA_CENTER
from reportlab.lib.pagesizes import landscape
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfbase import pdfmetrics
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Image, Table, TableStyle, PageBreak, KeepTogether

ROOT = Path(r"C:\Users\amano\Documents\Amanorsac Studio\APPS\Amanorsac Plugins")
OUT = ROOT / "output" / "pdf" / "Amanorsac_Analog_Collection_Guide.pdf"

APPS = [
    dict(id="A01", name="HERITAGE EQ", type="Five-band analog equalizer", model="A flexible large-format console EQ approach: shelves on the outside, musical bell bands in the middle, filter section, gain staging and selectable analog color.", uses="Shape an entire mix, clean low-end rumble, give vocals presence, tighten drums, or add controlled air.", alt="Waves API 550 / 560, Waves Scheps 73; UAD API 500 Series EQ and Pultec Passive EQ Collection.", accent="#D99A31"),
    dict(id="A02", name="IRON PRE", type="Transformer preamp and tone shaper", model="The weight, rounding and harmonic density associated with transformer-coupled microphone and line preamps - an 'iron' front-end concept rather than a clone of one named unit.", uses="Warm a vocal, make a DI bass feel larger, add density to synths, or give a sterile recording a more finished front end.", alt="Waves Scheps 73 and NLS; UAD API Preamp and Neve 1073 Preamp & EQ Collection.", accent="#C76B3B"),
    dict(id="A03", name="CONSOLE ONE", type="Console channel-strip color", model="The workflow and tone stages of a large-format analog console channel: input drive, broad EQ, dynamics and output trim in one place.", uses="Build a consistent console-like mix across many tracks, add gentle glue to stems, or use as a fast everyday channel strip.", alt="Waves SSL EV2 Channel, Waves NLS; UAD API Vision Channel Strip and SSL 4000 E Channel Strip.", accent="#D7B65C"),
    dict(id="A04", name="REELSAT", type="Tape saturation and tape-machine color", model="A multistage recording-tape concept: drive into soft compression, harmonics, tonal rounding and machine-style movement.", uses="Thicken drum buses, smooth harsh synths, add density to vocals, or provide subtle record-like cohesion on a mix bus.", alt="Waves J37 Tape and Kramer Master Tape; UAD Ampex ATR-102 and Studer A800.", accent="#A4653B"),
    dict(id="A05", name="VALVE DRIVE", type="Tube/valve saturation", model="The gradual soft clipping and harmonic bloom associated with valve amplification, from gentle warmth through purposeful drive.", uses="Make vocals richer, bring guitars forward, energize bass, or add a controlled edge to parallel effects.", alt="Waves Abbey Road Saturator and Berzerk; UAD Thermionic Culture Vulture and Capitol Mastering Compressor's tube color.", accent="#D07935"),
    dict(id="A06", name="STRIKE FET", type="Fast FET compressor", model="A fast, assertive FET limiter/compressor family: punchy attack behavior, strong transient control and a forward midrange attitude.", uses="Punch up drums, control a bright vocal, make bass more urgent, or use parallel compression for excitement.", alt="Waves CLA-76 and API 2500; UAD 1176 Classic Limiter Collection and Empirical Labs EL8 Distressor.", accent="#D75A35"),
    dict(id="A07", name="LUMEN OPTO", type="Optical leveling compressor", model="The smooth, program-dependent gain control of optical levelers, with a gentle tube-like sense of weight and a slower musical recovery.", uses="Lead vocals, bass, acoustic guitar, pads and gentle bus leveling where smoothness matters more than aggression.", alt="Waves CLA-2A and PuigChild 670; UAD Teletronix LA-2A Leveler Collection and Manley Variable Mu.", accent="#75B875"),
    dict(id="A08", name="BUSFORGE VCA", type="VCA bus compressor", model="A clean, controllable VCA bus-compressor approach designed for punch, envelope shaping and 'glue' across stereo material.", uses="Mix bus glue, drum bus punch, rhythm section control, stem shaping and transparent corrective compression.", alt="Waves SSL G-Master Buss Compressor and API 2500; UAD SSL 4000 G Bus Compressor and API 2500 Bus Compressor.", accent="#64A8CB"),
    dict(id="A09", name="SILK PASSIVE EQ", type="Passive program equalizer", model="The broad, sweet boost-and-attenuate behavior of passive tube EQs. Its curves are intended for musical tone choices rather than surgical correction.", uses="Add low-end weight without mud, open a vocal or mix with air, sweeten acoustic instruments and finish a master bus.", alt="Waves PuigTec EQs and Abbey Road RS56; UAD Pultec Passive EQ Collection and Manley Massive Passive.", accent="#C6B080"),
    dict(id="A10", name="PLATE FOUR", type="Plate reverberation", model="A classic plate-reverb concept: smooth, dense reflections with a bright musical tail, inspired by the studio plate tradition rather than an exact hardware copy.", uses="Place a vocal forward in a mix, add body to snare, create a lush short room-like plate, or make synths wider and deeper.", alt="Waves Abbey Road Reverb Plates and H-Reverb; UAD EMT 140 Classic Plate Reverberator and Pure Plate Reverb.", accent="#A97BC7"),
]

def para(text, style):
    return Paragraph(text, style)

def labels_for(app_id):
    spec = json.loads((ROOT / "specs" / "plugins" / f"{app_id}.json").read_text(encoding="utf-8"))
    labels = []
    for item in spec.get("parameters", []):
        label = item.get("label", "")
        # Specs may carry an internal parameter id before the printed caption.
        # The guide should show only the plain-English control name.
        if " " in label and ("_" in label.split(" ", 1)[0] or "." in label.split(" ", 1)[0]):
            label = label.split(" ", 1)[1]
        if label and label.lower() not in {x.lower() for x in labels}:
            labels.append(label)
    return labels[:12]

def page_num(canvas, doc):
    canvas.saveState()
    canvas.setStrokeColor(colors.HexColor("#3A4147")); canvas.line(34, 28, 758, 28)
    canvas.setFont("Helvetica", 8); canvas.setFillColor(colors.HexColor("#5A6670"))
    canvas.drawString(36, 16, "AMANORSAC STUDIO - ANALOG COLLECTION GUIDE")
    canvas.drawRightString(756, 16, f"PAGE {doc.page}")
    canvas.restoreState()

def build():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    doc = SimpleDocTemplate(str(OUT), pagesize=landscape((612, 792)), rightMargin=34, leftMargin=34, topMargin=26, bottomMargin=35)
    ss = getSampleStyleSheet()
    title = ParagraphStyle("title", parent=ss["Heading1"], fontName="Helvetica-Bold", fontSize=25, leading=27, textColor=colors.HexColor("#D9B46B"), spaceAfter=3)
    kicker = ParagraphStyle("kicker", parent=ss["Normal"], fontName="Helvetica-Bold", fontSize=9, leading=12, textColor=colors.HexColor("#738A98"), spaceAfter=5)
    head = ParagraphStyle("head", parent=ss["Heading2"], fontName="Helvetica-Bold", fontSize=11, leading=13, textColor=colors.HexColor("#D9B46B"), spaceAfter=3)
    body = ParagraphStyle("body", parent=ss["BodyText"], fontName="Helvetica", fontSize=9, leading=12, textColor=colors.HexColor("#E5E1D4"))
    tiny = ParagraphStyle("tiny", parent=body, fontSize=7.7, leading=10, textColor=colors.HexColor("#C9D1D2"))
    guide = []
    for app in APPS:
        # Use the rendered product surface rather than the transparent faceplate layer.
        # This keeps every knob and label intact when ReportLab embeds the artwork.
        face = ROOT / "artifacts" / "ui" / f"{app['id']}_aligned.png"
        labels = labels_for(app["id"])
        guide.append(para(f"{app['id']}  |  ANALOG COLLECTION", kicker))
        guide.append(para(app["name"], title))
        guide.append(para(app["type"], ParagraphStyle("type"+app["id"], parent=body, fontName="Helvetica-Bold", fontSize=12, leading=15, textColor=colors.HexColor(app["accent"]), spaceAfter=9)))
        img = Image(str(face), width=352, height=217, kind='proportional')
        controls = "<br/>".join([f"<b>{x}</b>" for x in labels]) or "See the product panel for its live controls."
        control_box = Table([[para("<b>WHAT YOU SEE ON THE PANEL</b><br/><br/>" + controls, tiny)]], colWidths=[354], rowHeights=[217])
        control_box.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,-1),colors.HexColor('#182126')),('BOX',(0,0),(-1,-1),.8,colors.HexColor(app['accent'])),('LEFTPADDING',(0,0),(-1,-1),13),('RIGHTPADDING',(0,0),(-1,-1),13),('TOPPADDING',(0,0),(-1,-1),13)]))
        grid = Table([[img, control_box]], colWidths=[370, 354], hAlign='LEFT')
        grid.setStyle(TableStyle([('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),0),('RIGHTPADDING',(0,0),(-1,-1),0)]))
        guide += [grid, Spacer(1, 9)]
        info = [
            [para("MODELED AFTER", head), para("BEST FOR", head), para("MARKET ALTERNATIVES", head)],
            [para(app["model"], body), para(app["uses"], body), para(app["alt"], body)],
        ]
        t = Table(info, colWidths=[241, 241, 242])
        t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),colors.HexColor('#273137')),('BACKGROUND',(0,1),(-1,1),colors.HexColor('#121819')),('BOX',(0,0),(-1,-1),.5,colors.HexColor('#59666A')),('INNERGRID',(0,0),(-1,-1),.4,colors.HexColor('#3D474B')),('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),11),('RIGHTPADDING',(0,0),(-1,-1),11),('TOPPADDING',(0,0),(-1,-1),7),('BOTTOMPADDING',(0,0),(-1,-1),8)]))
        guide += [t, Spacer(1, 7), para("<b>Quick start:</b> Start with input and output close to unity gain. Move one control at a time, then A/B at the same loudness. The competitor names are practical alternatives in the same category; Amanorsac is not affiliated with them.", tiny), PageBreak()]
    # Quick start pages
    guide += [para("ANALOG COLLECTION", kicker), para("Quick Start User Guide", title), para("A simple way to get useful results", ParagraphStyle("sub", parent=body, fontSize=13, leading=16, textColor=colors.HexColor("#7FB1C7"), spaceAfter=18))]
    start_rows = [[para("1. SET LEVEL", head), para("2. CHOOSE THE JOB", head), para("3. MOVE SLOWLY", head)], [para("Keep input and output close in loudness. Louder can sound better even when it is not.", body), para("Use one processor for one obvious reason: EQ, color, compression, or space.", body), para("Start with modest moves. Analog-style tools are usually most useful before they look extreme.", body)], [para("4. A/B MATCH", head), para("5. WATCH METERS", head), para("6. PRINT OR RECALL", head)], [para("Bypass often. Match output level. Keep the change only if it improves the song.", body), para("On compressors, use gain reduction as a guide. On saturation, listen for harshness and loss of punch.", body), para("Save a preset for repeatable sounds. Keep a clean starting point for every new source.", body)]]
    st = Table(start_rows, colWidths=[241,241,242])
    st.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),colors.HexColor('#1C2A31')),('BACKGROUND',(0,2),(-1,2),colors.HexColor('#1C2A31')),('BACKGROUND',(0,1),(-1,1),colors.HexColor('#111718')),('BACKGROUND',(0,3),(-1,3),colors.HexColor('#111718')),('BOX',(0,0),(-1,-1),.6,colors.HexColor('#59717E')),('INNERGRID',(0,0),(-1,-1),.4,colors.HexColor('#39464B')),('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),14),('RIGHTPADDING',(0,0),(-1,-1),14),('TOPPADDING',(0,0),(-1,-1),12),('BOTTOMPADDING',(0,0),(-1,-1),14)]))
    guide += [st, Spacer(1,20), para("<b>A helpful chain:</b> Iron Pre or Console One for color -> Heritage EQ or Silk Passive EQ for tone -> Strike FET, Lumen Opto, or Busforge VCA for dynamics -> Reelsat or Valve Drive for optional extra character -> Plate Four on an aux/send for space.", body), PageBreak()]
    guide += [para("ANALOG COLLECTION", kicker), para("Fast Control Glossary", title)]
    gloss = [["Control", "Plain-English meaning", "Practical tip"], ["Input / Drive", "How hard you feed the processor.", "More can mean more color or compression. Re-check output level."], ["Output / Trim", "The level leaving the plug-in.", "Use it to loudness-match bypass."], ["Attack", "How quickly a compressor reacts.", "Slower often preserves punch; faster catches peaks."], ["Release", "How soon compression lets go.", "Set it to breathe with the groove, not pump by accident."], ["Ratio", "How firmly compression turns down peaks.", "Start lower for natural control; increase for obvious effect."], ["Q", "How wide or narrow an EQ band is.", "Wide is musical shaping; narrow is more corrective."], ["Mix", "Blend of processed and dry signal.", "Useful for parallel compression, saturation and reverb."], ["Oversampling", "Extra internal processing for some nonlinear effects.", "Use higher modes when needed; lower modes save CPU."], ["A/B", "Two separate settings for comparison.", "Make decisions at equal loudness, not equal knob positions."]]
    gl = Table([[para(f"<b>{x}</b>", head) for x in gloss[0]]] + [[para(x,body) for x in row] for row in gloss[1:]], colWidths=[155,280,289])
    gl.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),colors.HexColor('#26343A')),('BACKGROUND',(0,1),(-1,-1),colors.HexColor('#111718')),('GRID',(0,0),(-1,-1),.35,colors.HexColor('#3D4F54')),('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),10),('RIGHTPADDING',(0,0),(-1,-1),10),('TOPPADDING',(0,0),(-1,-1),6),('BOTTOMPADDING',(0,0),(-1,-1),7)]))
    guide += [gl, Spacer(1,12), para("<b>Safety note:</b> Watch your monitoring level when using Drive, Input, Output, saturation or feedback-related effects. If a sound suddenly becomes harsh, unstable or much louder, reduce the drive/input first and then reset output to a safe level.", tiny)]
    doc.build(guide, onFirstPage=page_num, onLaterPages=page_num)
    print(OUT)

if __name__ == "__main__":
    build()
