// Headless encounter dumper: resolves journal encounter world positions for a
// map using wow.export's own DB2 layer, and writes them as JSON for downstream
// tooling (e.g. placing cameras in Blender over an exported map).
//
// Same world yards the map exporter writes into ADT/WMO OBJ vertices, so the
// output can be used to frame geometry directly.
//
// Run:  bun headless-encounters.js <install-dir> <product> <map-id> <out.json>

const path = require('path');
const fsp = require('fs/promises');
const os = require('os');

global.BUILD_RELEASE = true;
process.env.BUILD_RELEASE = 'true';

const DATA_PATH = path.join(os.tmpdir(), 'wowexport-headless');
require('fs').mkdirSync(DATA_PATH, { recursive: true });

// constants.INSTALL_PATH resolves to the runtime exe dir when run headless, so
// the native mmap addon cannot be found; point it at the installed one.
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

// NW.js globals the app modules touch at load time.
global.nw = {
	__dirname: path.join(__dirname, 'src'),
	App: {
		dataPath: DATA_PATH,
		manifest: { version: '0.0.0-headless' },
		argv: [],
	},
	Shell: { openItem: () => {}, openExternal: () => {} },
	Window: { get: () => ({ on: () => {}, showDevTools: () => {} }) },
};

// Use the shipped defaults rather than a hand-written subset: the CASC and DB2
// layers read a number of URLs from config (listfile, DBD manifest and its
// fallback), and a missing one fails quietly rather than loudly.
const default_config = JSON.parse(
	require('fs').readFileSync(path.join(__dirname, 'src', 'default_config.jsonc'), 'utf8')
		.replace(/^\s*\/\/.*$/gm, '')
);

const core = require('./src/js/core');
core.view = core.view || {};
Object.assign(core.view, {
	config: Object.assign({}, default_config, {
		overwriteFiles: true,
		removePathSpaces: true,
		pathFormat: 'win32',
	}),
	casc: null,
	selectedCDNRegion: { tag: 'us' },
	cacheSize: 0,
	cascFileCount: 0,
});
core.view.$watch = () => () => {};
core.view.$set = (obj, key, val) => { obj[key] = val; };
core.progressLoadingScreen = async () => {};
core.setLoadProgress = async () => {};
core.createProgress = () => ({ step: async () => {} });
core.block = async (fn) => await fn();

const CASCLocal = require('./src/js/casc/casc-source-local');

async function main() {
	const [installDir, product, mapIdRaw, outFile] = process.argv.slice(2);
	if (!installDir || !product || !mapIdRaw || !outFile) {
		console.error('usage: bun headless-encounters.js <install-dir> <product> <map-id> <out.json>');
		process.exit(2);
	}

	const mapID = Number(mapIdRaw);

	const casc = new CASCLocal(installDir);
	core.view.casc = casc;
	casc.locale = require('./src/js/casc/locale-flags').flags.enUS;
	await casc.init();

	const idx = casc.builds.findIndex(b => b.Product === product);
	if (idx === -1) {
		console.error(`product ${product} not found. Available:`, casc.builds.map(b => b.Product));
		process.exit(1);
	}

	// Journal tables are resolved by name, so unlike the fdid-only extractor
	// this needs the listfile as well as the core CASC tables.
	casc.build = casc.builds[idx];
	const BuildCache = require('./src/js/casc/build-cache');
	casc.cache = new BuildCache(casc.build.BuildKey);
	await casc.cache.init();
	await casc.loadConfigs();
	await casc.loadIndexes();
	await casc.loadEncoding();
	await casc.loadRoot();
	await casc.loadListfile(casc.build.BuildConfig);

	// DB2 tables are looked up by table name via the DBD manifest, not the
	// listfile; without this every Journal* read fails and the encounter
	// resolver quietly returns an empty list.
	await require('./src/js/casc/dbd-manifest').prepareManifest();
	console.log('CASC loaded');

	const journal = require('./src/js/journal-encounters');
	const positions = await journal.get_encounter_world_positions(mapID);

	// The journal tables drive the map markers, but combat logs (and anything
	// keying off them) identify a boss by its DungeonEncounter id, which is a
	// different table with its own ids. Match them up by name so the output
	// carries the id a log reader will actually look for.
	const db2 = require('./src/js/casc/db2');
	const dungeon_ids = new Map();
	for (const row of (await db2.DungeonEncounter.getAllRows()).values()) {
		if (row.MapID === mapID && row.Name_lang)
			dungeon_ids.set(row.Name_lang.toLowerCase(), row.ID);
	}

	const encounters = positions.map(p => ({
		encounter_id: dungeon_ids.get((p.label ?? '').toLowerCase()) ?? null,
		label: p.label,
		world_x: p.world_x,
		world_y: p.world_y,
	}));

	console.log(`map ${mapID}: ${encounters.length} encounters`);
	for (const e of encounters) {
		const id = e.encounter_id === null ? '   (no id)' : String(e.encounter_id).padStart(9);
		console.log(`  ${id}  ${e.world_x.toFixed(1).padStart(9)} ${e.world_y.toFixed(1).padStart(9)}  ${e.label}`);
	}

	const missing = encounters.filter(e => e.encounter_id === null);
	if (missing.length)
		console.log(`warning: ${missing.length} encounter(s) had no DungeonEncounter match`);

	await fsp.writeFile(outFile, JSON.stringify({ map_id: mapID, encounters }, null, '\t'));
	console.log(`wrote ${outFile}`);
}

main().catch(e => {
	console.error(e.stack || e);
	process.exit(1);
});
