import fs from 'node:fs/promises';
import {FileBlob,SpreadsheetFile} from '@oai/artifact-tool';
const root='C:/Users/amano/Documents/Amanorsac Studio/APPS/Amanorsac Plugins';
const dir=root+'/outputs/waves_analog_preset_register';
const file=dir+'/Waves_Analog_Preset_Register.xlsx';
const wb=await SpreadsheetFile.importXlsx(await FileBlob.load(file));
const before=await wb.render({sheetName:'Preset Register',range:'C4:D9',scale:1});
await fs.writeFile(dir+'/before-decode.png',new Uint8Array(await before.arrayBuffer()));
const maps=JSON.parse(await fs.readFile(root+'/tools/waves_parameter_maps.json','utf8'));
const register=wb.worksheets.getItem('Preset Register');
const data=register.getUsedRange().values;
const details=[];let numeric=0,unresolved=0;
for(let r=4;r<data.length;r++){
 const row=data[r];if(!row[2]||!row[4])continue;
 const params=maps[row[2]]?.parameters;if(!params)continue;
 const raw=String(row[4]).trim().split(/\s+/);
 for(let i=0;i<raw.length;i++){
  const p=params[i],v=Number(raw[i]);let val=null,status='Unresolved',note='';
  const scale=p?.ScaleType?.trim();const lo=Number(p?.Minimum),hi=Number(p?.Maximum);
  if(p&&Number.isFinite(v)){
   if(/VU|CLIP|METER|GRAPH|SELECTED|UNUSED/.test(p.name)){status='Stored UI state';note='Not a sound-setting control';}
   else if(scale==='toggle'){if(v>=lo&&v<=hi){val=v;status='Enum code';note='Display option names need verification';}else note='Value outside current definition: possible legacy version difference';}
   else if(['linear','logarithmic'].includes(scale)&&v>=0&&v<=1000&&Number.isFinite(lo)&&Number.isFinite(hi)&&(scale==='linear'||lo>0)){
    val=scale==='linear'?lo+(hi-lo)*v/1000:lo*Math.pow(hi/lo,v/1000);val=Number(val.toPrecision(8));status='Calculated estimate';note='Current ParamXML order; state/1000 scaling. Legacy v3.5 alignment needs plugin verification.';numeric++;
   }else note='Custom scale or legacy state: conversion not verified';
  }
  if(status==='Unresolved')unresolved++;
  details.push([r+1,row[2],row[3],i+1,p?.name||'Unknown',p?.Label||p?.name||'Unknown',v,val,p?.Units||'',scale||'',status,note,maps[row[2]].source]);
 }
}
const sh=wb.worksheets.add('Named Parameters');sh.showGridLines=false;
const headers=['Register row','Waves plugin','Preset name','State position','Parameter ID','Control label','Raw state','Calculated value','Unit','Scale','Decode status','Interpretation','Definition source'];
sh.getRange('A1:M1').values=[headers];sh.getRange('A2').write(details);
const end=details.length+1;
sh.getRange(`A1:M${end}`).format.font={name:'Arial',size:10};
sh.getRange(`A1:M${end}`).format.rowHeight=23;
sh.getRange('A1:M1').format={fill:'#24323E',font:{name:'Arial',size:10,bold:true,color:'#FFFFFF'},rowHeight:30};
for(const [c,w]of Object.entries({A:13,B:20,C:32,D:15,E:36,F:25,G:12,H:20,I:12,J:20,K:24,L:95,M:95}))sh.getRange(`${c}1:${c}${end}`).format.columnWidth=w;
sh.getRange(`H2:H${end}`).setNumberFormat('0.0000');
sh.tables.add(`A1:M${end}`,true,'NamedWavesParameters');sh.freezePanes.freezeRows(1);
wb.worksheets.getItem('Overview').getRange('A14').values=[['Named Parameters now contains control IDs, units and calculated estimates. Custom scales remain unresolved. Legacy preset alignment still needs verification in Waves.']];
register.getRange('A2').values=[['Use Named Parameters for readable controls. Numeric conversions are estimates pending legacy-version verification. Yellow columns remain editable.']];
wb.recalculate();
console.log((await wb.inspect({kind:'table',range:'Named Parameters!A1:K7',tableMaxRows:7,tableMaxCols:11})).ndjson);
const img=await wb.render({sheetName:'Named Parameters',range:'B1:K8',scale:1});await fs.writeFile(dir+'/decoded-preview.png',new Uint8Array(await img.arrayBuffer()));
const backup=dir+'/Waves_Analog_Preset_Register.before-decoding.xlsx';try{await fs.copyFile(file,backup,1);}catch(e){if(e.code!=='EEXIST')throw e;}
await(await SpreadsheetFile.exportXlsx(wb)).save(file);
console.log(JSON.stringify({parameterRows:details.length,numericEstimates:numeric,unresolved}));
