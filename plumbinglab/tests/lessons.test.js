// Verifies every lesson is solvable with the engine embedded in index.html.
// Run: node plumbinglab/tests/lessons.test.js
const fs=require('fs'),path=require('path'),vm=require('vm');
const html=fs.readFileSync(path.join(__dirname,'..','index.html'),'utf8');
const src=html.match(/<script id="engine">([\s\S]*?)<\/script>/)[1];
const mod={exports:{}};vm.runInNewContext(src,{module:mod,Math,Map,Set,JSON,Object,Array,Number});
const E=mod.exports,nid=E.nid,ek=E.ek;
const fixIds=S=>S.fixtures.filter(f=>!['meter','sewer','heater'].includes(f.type)).map(f=>f.id);
const solutions=[
 S=>{E.run(S,'cold',.75,[[4,22],[29,22],[29,19]]);E.setAttr(S,'cold',5,22,{v:'open'});},
 S=>{E.run(S,'cold',.75,[[4,22],[30,22],[30,13],[19,13]]);},
 S=>{E.run(S,'cold',.75,[[8,22],[8,15],[7,15]]);E.setAttr(S,'cold',8,16,{v:'open'});E.run(S,'hot',.75,[[6,15],[6,14],[20,14],[20,17]]);E.run(S,'hot',.5,[[14,14],[14,19]]);},
 S=>{E.setAttr(S,'drain',11,10,{trap:true});E.run(S,'vent',1.5,[[14,10],[14,2]]);},
 S=>{E.run(S,'drain',3,[[13,21],[13,23]]);E.run(S,'drain',1.5,[[21,21],[17,21]]);for(let x=6;x<9;x++)S.edges.drain.get(ek(nid(x,23),nid(x+1,23))).slope=.25;},
 S=>E.buildSample(S),
 S=>{E.setAttr(S,'cold',7,22,{v:'prv'});E.setAttr(S,'cold',8,18,{v:'xtank'});},
 S=>{S.attrs.hot.delete(nid(23,13));E.setAttr(S,'drain',29,19,{trap:true});E.run(S,'vent',1.5,[[22,7],[22,2]]);
     for(let x=8;x<12;x++)S.edges.drain.get(ek(nid(x,24),nid(x+1,24))).slope=.125;for(let y=19;y<22;y++)S.edges.cold.delete(ek(nid(12,y),nid(12,y+1)));},
 S=>{E.run(S,'drain',2,[[11,10],[21,10]]);E.run(S,'vent',1.5,[[19,10],[19,2]]);},
 S=>E.buildHill(S),
 S=>E.buildTwoBath(S,1),
];
let fail=0;
E.LESSONS.forEach((L,i)=>{const S=E.newState();S.supplyOn=L.supplyOn;S.drainOn=L.drainOn;S.staticPsi=L.psi;E.baseFx(S);L.setup(S);
  const before=L.goals.map(([,f])=>!!f(E.analyze(S),S));
  solutions[i](S);S.waterOn=true;fixIds(S).forEach(id=>S.open.add(id));
  const A=E.analyze(S),after=L.goals.map(([,f])=>!!f(A,S));const ok=after.every(Boolean)&&!before.every(Boolean);
  if(!ok)fail++;console.log(`${ok?'PASS':'FAIL'}  ${i+1}. ${L.title}  [${after.map(b=>b?'✓':'✗').join('')}]  $${A.cost.total}`);});
process.exit(fail?1:0);
