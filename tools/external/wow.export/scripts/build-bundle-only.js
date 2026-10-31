// Standalone: produce just the app.js bundle exactly like the release build
// (build.js Bun.build), without the NW.js/native-addon exe packaging.
const path = require('path');

const jsEntry = path.resolve('src/app.js');
const outdir = path.resolve('bin/bundle-only');

const result = await Bun.build({
	entrypoints: [jsEntry],
	outdir,
	target: 'node',
	format: 'cjs',
	define: { 'process.env.BUILD_RELEASE': '"true"' }
});

if (!result.success) {
	console.error('BUILD FAILED');
	for (const msg of result.logs) console.error(msg);
	process.exit(1);
}
for (const output of result.outputs) console.log('Created bundle:', output.path);
console.log('OK');
