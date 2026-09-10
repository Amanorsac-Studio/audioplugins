import json, math
from pathlib import Path
import xml.etree.ElementTree as ET

root=Path(r'C:\Program Files (x86)\Waves\Plug-Ins V15')
out=Path(__file__).parent/'waves_parameter_maps.json'
maps={}
for plugin,bundle in [('Q10','Q10'),('C1','C1'),('AudioTrack','AudioTrack'),('TrueVerb','TrueVerb'),('Renaissance EQ','REQ')]:
    source=root/(bundle+'.bundle')/'Contents/Resources/ParamXML/1000.xml'
    nodes=ET.parse(source).getroot().findall('Parameter')
    byname={n.attrib['Name']:n for n in nodes}
    def resolve(n):
        d=resolve(byname[n.attrib['Parent']]).copy() if 'Parent' in n.attrib else {}
        for ch in n:
            if ch.tag=='Scale':
                for x in ch: d[x.tag]=x.text
            elif ch.tag in ('Label','Units','DisplayFunction'): d[ch.tag]=ch.text
        d['name']=n.attrib['Name']
        return d
    params=[resolve(n) for n in nodes]
    checks=[]
    for n,p in zip(nodes,params):
        reset=n.find('ResetValue'); initial=n.find('InitialValue')
        if reset is not None and initial is not None and reset.get('Type')=='RealWorld' and initial.get('Type')=='state' and p.get('ScaleType') in ('linear','logarithmic'):
            lo=float(p['Minimum']);hi=float(p['Maximum']);v=float(initial.text)/1000
            val=lo+(hi-lo)*v if p['ScaleType']=='linear' else lo*(hi/lo)**v
            checks.append([p['name'],float(initial.text),float(reset.text),val])
    maps[plugin]={'source':str(source),'parameters':params,'reset_crosschecks':checks}
out.write_text(json.dumps(maps,indent=2),encoding='utf8')
for k,v in maps.items(): print(k,len(v['parameters']),'scales',sorted({p.get('ScaleType','') for p in v['parameters']}),'reset comparisons',v['reset_crosschecks'][:4])
