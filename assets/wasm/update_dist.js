#!/usr/bin/env node
const fs = require('fs');
const path = require('path');

const projectRoot = path.resolve(__dirname, '../..');
const htmlPath = path.join(projectRoot, 'dist/sega_cd_demo.html');
const mainPath = path.join(projectRoot, 'out_cd/main.bin');
const subPath = path.join(projectRoot, 'out_cd/sub.bin');
const targetDeploy = '/var/www/apps/sega-dev/sega-cd/index.html';

if (!fs.existsSync(htmlPath)) {
  console.error("dist/sega_cd_demo.html not found!");
  process.exit(1);
}

let html = fs.readFileSync(htmlPath, 'latin1');
const mainB64 = fs.readFileSync(mainPath).toString('base64');
const subB64 = fs.readFileSync(subPath).toString('base64');

// We search for our marker or the bootstrap function in dist/sega_cd_demo.html
const injectionCode = `/*INJECT_ROM_START*/(function(){try{var d=function(b){var s=atob(b),u=new Uint8Array(s.length);for(var i=0;i<s.length;i++)u[i]=s.charCodeAt(i);return u;};if(typeof FS!=="undefined"){try{FS.mkdirTree("/rom");}catch(e){}try{FS.unlink("/rom/main.bin");}catch(e){}try{FS.unlink("/rom/sub.bin");}catch(e){}FS.createDataFile("/rom","main.bin",d("${mainB64}"),true,true,true);FS.createDataFile("/rom","sub.bin",d("${subB64}"),true,true,true);console.log("Injected updated main.bin & sub.bin into /rom/");}}catch(e){console.warn("ROM injection failed:",e);}})();/*INJECT_ROM_END*/`;

const markerStart = html.indexOf('/*INJECT_ROM_START*/');
const markerEnd = html.indexOf('/*INJECT_ROM_END*/');

if (markerStart !== -1 && markerEnd !== -1) {
  html = html.substring(0, markerStart) + injectionCode + html.substring(markerEnd + '/*INJECT_ROM_END*/'.length);
} else {
  // Inject into boot function right before Module.callMain([])
  const target = 'Module.callMain([])';
  const idx = html.indexOf(target);
  if (idx === -1) {
    console.error("Could not find Module.callMain([]) in HTML!");
    process.exit(1);
  }
  html = html.substring(0, idx) + injectionCode + html.substring(idx);
}

fs.writeFileSync(htmlPath, html, 'latin1');
console.log(`Updated ${htmlPath} with latest main.bin and sub.bin (${fs.statSync(htmlPath).size} bytes)`);

if (fs.existsSync(path.dirname(targetDeploy))) {
  fs.writeFileSync(targetDeploy, html, 'latin1');
  console.log(`Deployed to ${targetDeploy} (${fs.statSync(targetDeploy).size} bytes)`);
}
