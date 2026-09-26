// Render regression checks in a minimal DOM double. No browser or hardware actions.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');

async function main() {
  const html = fs.readFileSync('workbench.html', 'utf8');
  const script = html.split('<script>')[1].split('</script>')[0];
  const state = await (await fetch('http://127.0.0.1:8765/api/state')).json();
  const elements = new Map([...html.matchAll(/id="([^"]+)"/g)].map(m => [m[1], {
    innerHTML: '', textContent: '', value: '', options: [], dataset: {}, style: {},
    classList: { toggle() {} }, scrollHeight: 0, scrollTop: 0, clientHeight: 0,
  }]));
  const context = vm.createContext({
    document: {getElementById: id => elements.get(id), querySelectorAll: () => [], activeElement: null},
    fetch: async () => ({ok: true, json: async () => state}),
    setInterval() {}, console,
  });
  vm.runInContext(script, context);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(elements.get('error').textContent, '');
  assert.match(elements.get('capabilities').innerHTML, /AT\+CIPSSLSIZE\?/);
  assert.match(elements.get('capabilities').innerHTML, /CIPSSLSIZE:2048/);
  assert.match(elements.get('capabilities').innerHTML, /ERROR/);
  assert.match(elements.get('capabilities').innerHTML, /SNTP \/ NTP/);
  assert.match(elements.get('capabilities').innerHTML, /AT\+CIPSNTPCFG\?/);
  assert.match(elements.get('candidates').innerHTML, /Bereit im Firmwareordner/);
  assert.equal(elements.get('prepare').disabled, false);
  assert.equal(elements.get('firmwareModule').value, 'ESP-01 / ESP-01S');
  // Check evidence rendering escapes all firmware-provided content.
  assert.equal(vm.runInContext("probeTable([{command:'AT',status:'OK',response:'<script>alert(1)</script>'}]).includes('<script>')", context), false);
  console.log('UI render OK: firmware selection enabled, raw responses visible, HTML escaped.');
}
main().catch(error => {console.error(error); process.exitCode = 1;});
