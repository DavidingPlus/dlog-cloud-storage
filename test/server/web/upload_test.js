const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const templatePath = path.join(__dirname, '../../../src/server/web/template.html');
const template = fs.readFileSync(templatePath, 'utf8');
const script = [...template.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/gi)].map(match => match[1]).join('\n');


// 直接执行真实页面脚本，模拟 DOM 和 fetch；不监听端口，也不依赖第三方包或真实浏览器。
for (const pageUrl of [
    'http://127.0.0.1:8081/',
    'http://localhost:8081/',
    'http://cloud.example:8081/',
    'https://cloud.example/'
]) {
    for (const storageType of ['uncompressed', 'compressed']) {
        test(`${storageType} upload stays on ${pageUrl}`, async () => {
            const file = {
                name: '中文 空格+%2F.bin',
                bytes: Buffer.from([0x41, 0x00, 0xff, 0x7a])
            };
            const requests = [];
            const alerts = [];
            let reloads = 0;
            const context = vm.createContext({
                document: {
                    getElementById(id) {
                        assert.equal(id, 'fileInput');
                        return { files: [file] };
                    },
                    querySelector(selector) {
                        assert.equal(selector, 'input[name="storageType"]:checked');
                        return { value: storageType };
                    }
                },
                btoa: value => Buffer.from(value, 'binary').toString('base64'),
                fetch: async (url, options) => {
                    requests.push({ url: new URL(url, pageUrl), options });
                    return { ok: true };
                },
                location: { href: pageUrl, reload: () => ++reloads },
                alert: message => alerts.push(message),
                console: { log() { }, error() { } }
            });

            vm.runInContext(script, context, { filename: templatePath });
            await context.uploadFile();

            assert.equal(requests.length, 1);
            assert.equal(requests[0].url.href, new URL('/upload', pageUrl).href);
            assert.equal(requests[0].options.method, 'POST');
            assert.equal(requests[0].options.headers.StorageType, storageType);
            assert.equal(requests[0].options.headers.FileName, Buffer.from(file.name, 'utf8').toString('base64'));
            assert.equal(requests[0].options.body, file);
            assert.equal(reloads, 1);
            assert.deepEqual(alerts, []);
        });
    }
}
