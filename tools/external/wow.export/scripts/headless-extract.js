// Headless M2 extractor: drives wow.export's CASCLocal + M2Exporter without the
// GUI, to pull a list of .m2 fileDataIDs from the live install and write OBJs.
// wow.export's own CASC reader parses the CURRENT build correctly (unlike
// rustydemon, which is stuck on an older build hash).
//
// Run:  bun headless-extract.js <install-dir> <product> <fdid-list.txt> <out-dir>

const path = require('path');
const fsp = require('fs/promises');
const os = require('os');

// Redirect the native mmap.node require to the prebuilt addon shipped with the
// installed app. constants.INSTALL_PATH resolves to the node/bun exe dir when run
// headless (wrong), so mmap.js can't find the addon; intercept and point it at
// the real one.
// Build-time define normally injected by the bundler.
global.BUILD_RELEASE = true;
process.env.BUILD_RELEASE = 'true';

// The runtime-log writer opens <dataPath>/runtime.log at module load; ensure it exists.
const DATA_PATH = path.join(os.tmpdir(), 'wowexport-headless');
require('fs').mkdirSync(DATA_PATH, { recursive: true });

// The installed app keeps mmap.node beside wow.export.exe.
const MMAP_NODE = process.env.WOWEXPORT_MMAP_NODE;
if (!MMAP_NODE) {
	console.error('Set WOWEXPORT_MMAP_NODE to the mmap.node in your wow.export install.');
	process.exit(1);
}
const Module = require('module');
const _origLoad = Module._load;
Module._load = function (request, parent, isMain) {
	if (request.endsWith('mmap.node') || request === MMAP_NODE)
		return _origLoad.call(this, MMAP_NODE, parent, isMain);
	return _origLoad.call(this, request, parent, isMain);
};

// ---- stub the NW.js globals the modules touch at load time (before any require
// of the app source). constants.js reads nw.App.dataPath / manifest.version etc.
global.nw = {
	__dirname: path.join(__dirname, 'src'),
	App: {
		dataPath: path.join(os.tmpdir(), 'wowexport-headless'),
		manifest: { version: '0.0.0-headless' },
		argv: [],
	},
	Shell: { openItem: () => {}, openExternal: () => {} },
	Window: { get: () => ({ on: () => {}, showDevTools: () => {} }) },
};

// ---- stub the app "core" + globals the CASC/export modules reach for ----
const core = require('./src/js/core');
// core.view is normally a Vue reactive app-state; provide the fields the
// CASC/export path uses. (config flags, casc handle, cdn region.)
core.view = core.view || {};
Object.assign(core.view, {
	config: {
		modelsExportTextures: false,   // geometry only — no BLP fetch/convert
		modelsExportUV2: false,
		exportM2Meta: false,
		exportM2Bones: false,
		modelsExportCollision: false,
		overwriteFiles: true,
		enableSharedTextures: false,
		enableSharedChildren: false,
		removePathSpaces: true,
		pathFormat: 'win32',
	},
	casc: null,
	selectedCDNRegion: { tag: 'us' },
	cacheSize: 0,
	cascFileCount: 0,
});
// Vue reactivity shims used by the CASC layer.
core.view.$watch = () => () => {};
core.view.$set = (obj, key, val) => { obj[key] = val; };
// UI progress / toast are no-ops headless.
core.progressLoadingScreen = async () => {};
core.setLoadProgress = async () => {};
core.createProgress = () => ({ step: async () => {} });
core.block = async (fn) => await fn();

const CASCLocal = require('./src/js/casc/casc-source-local');
const M2Exporter = require('./src/js/3D/exporters/M2Exporter');
const listfile = require('./src/js/casc/listfile');

// A no-op ExportHelper (M2Exporter calls helper.isCancelled / setCurrentTask*).
const helper = {
	isCancelled: () => false,
	setCurrentTaskName: () => {},
	setCurrentTaskMax: () => {},
	setCurrentTaskValue: () => {},
	clearCurrentTask: () => {},
};

async function main() {
	const [installDir, product, fdidFile, outDir] = process.argv.slice(2);
	if (!installDir || !product || !fdidFile || !outDir) {
		console.error('usage: bun headless-extract.js <install-dir> <product> <fdid-list.txt> <out-dir>');
		process.exit(2);
	}

	await fsp.mkdir(outDir, { recursive: true });

	const fdids = (await fsp.readFile(fdidFile, 'utf8'))
		.split(/\r?\n/).map(s => s.trim()).filter(s => /^\d+$/.test(s)).map(Number);
	console.log(`extracting ${fdids.length} models from ${installDir} (${product})`);

	// --- init CASC against the local install ---
	const casc = new CASCLocal(installDir);
	core.view.casc = casc;
	// $watch is stubbed to a no-op, so set the locale the CASC layer expects
	// (enUS = 0x2) directly rather than via the config watcher.
	casc.locale = require('./src/js/casc/locale-flags').flags.enUS;
	await casc.init();

	// pick the build matching the requested product
	const idx = casc.builds.findIndex(b => b.Product === product);
	if (idx === -1) {
		console.error(`product ${product} not found. Available:`, casc.builds.map(b => b.Product));
		process.exit(1);
	}

	// Run only the load steps needed for fdid extraction: configs, indexes,
	// encoding, root. Skip listfile/DBD (network-heavy, only needed for
	// name<->fdid resolution, which we don't use — we export by fdid).
	casc.build = casc.builds[idx];
	const BuildCache = require('./src/js/casc/build-cache');
	casc.cache = new BuildCache(casc.build.BuildKey);
	await casc.cache.init();
	await casc.loadConfigs();
	await casc.loadIndexes();
	await casc.loadEncoding();
	await casc.loadRoot();
	console.log('CASC loaded (configs/indexes/encoding/root)');

	let ok = 0, fail = 0;
	const failed = [];
	for (const fdid of fdids) {
		const out = path.join(outDir, `model_${fdid}.obj`);
		try {
			const data = await casc.getFile(fdid, false, true);  // suppressLog
			const exporter = new M2Exporter(data, undefined, fdid);
			await exporter.exportAsOBJ(out, false, helper, null);
			ok++;
			if (ok % 10 === 0) console.log(`  ${ok}/${fdids.length} ...`);
		} catch (e) {
			fail++;
			failed.push([fdid, e.message]);
		}
	}

	console.log(`\nDONE: ${ok} exported, ${fail} failed`);
	if (failed.length) {
		console.log('failures:');
		for (const [f, m] of failed.slice(0, 20)) console.log(`  ${f}: ${m}`);
	}
}

main().catch(e => { console.error('FATAL', e); process.exit(1); });
