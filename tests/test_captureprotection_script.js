// Exercise the actual generated KWin program without touching a desktop.
// Run: node tests/test_captureprotection_script.js
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../src/captureprotection.cpp'), 'utf8');
const match = source.match(/R"JS\(([\s\S]*?)\)JS"/);
assert.ok(match, 'KWin program is present');
const program = match[1].replace('%1', '1234').replace('%2', '"test.service"').replace('%3', '"test-token"');

function window(pid, resourceClass, supported = true) {
    const result = {pid, resourceClass, listeners: []};
    if (supported) result.excludeFromCapture = false;
    result.excludeFromCaptureChanged = {connect(fn) { result.listeners.push(fn); }};
    return result;
}
function run(windows) {
    const reports = [];
    let onAdded;
    vm.runInNewContext(program, {
        workspace: {windowList: () => windows, windowAdded: {connect(fn) { onAdded = fn; }}},
        callDBus(service, endpoint, iface, method, token, ready) {
            assert.equal(service, 'test.service');
            assert.equal(token, 'test-token');
            reports.push(ready);
        }
    });
    return {reports, add: window => onAdded(window)};
}

const own = window(1234, 'm1zuki');
const renamed = window(1234, 'arbitrary-renamed-program');
const other = window(5678, 'm1zuki');
const normal = run([own, renamed, other]);
assert.equal(own.excludeFromCapture, true);
assert.equal(renamed.excludeFromCapture, true);
assert.equal(other.excludeFromCapture, false);
assert.deepEqual(normal.reports, [true]);
const later = window(1234, 'another-alias');
normal.add(later);
assert.equal(later.excludeFromCapture, true);
later.excludeFromCapture = false;
later.listeners[0]();
assert.equal(later.excludeFromCapture, true);
const unsupported = run([window(1234, 'm1zuki', false), other]);
assert.ok(unsupported.reports.length > 0);
assert.ok(unsupported.reports.every(ready => ready === false));
console.log('PASS: renamed executable, owned future windows, foreign PID, loss of exclusion, unsupported KWin');
