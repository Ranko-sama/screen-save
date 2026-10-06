const fs = require('node:fs');
const path = require('node:path');
const dir = path.join(__dirname, 'artifacts');
const read = name => fs.readFileSync(path.join(dir, name), 'utf8').trim().split(/\r?\n/).filter(Boolean).map(JSON.parse);
const median = values => {
  const a = [...values].sort((x,y)=>x-y);
  return a.length % 2 ? a[(a.length-1)/2] : (a[a.length/2-1]+a[a.length/2])/2;
};
const resource = read('gamma-resources-complete.jsonl');
const browser = read('gamma-browser-complete.jsonl');
const labels = {off:'未运行',normal:'正常',gamma:'Gamma',matrix:'颜色矩阵'};
const output = {};
for (const [mode,label] of Object.entries(labels)) {
  const r = resource.filter(x=>x.mode===mode), b=browser.filter(x=>x.mode===label && x.seconds===30);
  output[mode] = {resource_rounds:r.length,browser_rounds:b.length};
  for (const key of ['app_cpu_pct','app_private','app_working','dwm_cpu_pct','dwm_private','dwm_gpu'])
    output[mode][key] = {median:median(r.map(x=>x[key])),min:Math.min(...r.map(x=>x[key])),max:Math.max(...r.map(x=>x[key]))};
  for (const key of ['fps','frame_p95_ms','timer_p95_ms','work_median_ms','work_p95_ms'])
    output[mode][key] = median(b.map(x=>x[key]));
  output[mode].hidden_events = b.reduce((a,x)=>a+x.hidden_events,0);
  output[mode].blur_events = b.reduce((a,x)=>a+x.blur_events,0);
}
fs.writeFileSync(path.join(dir,'gamma-summary.json'),JSON.stringify(output,null,2)+'\n');
console.log(JSON.stringify(output,null,2));
