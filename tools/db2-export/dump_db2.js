// Copies raw DB2 tables out of a local WoW install using wow.export's CASC
// layer, so no window or manual export is needed. export_db2.py runs this and
// converts the result; it can also be run on its own.
//
// Run:  node dump_db2.js <wowexport-checkout> <install-dir> <product> <table-list.csv> <out-dir> [--no-cdn]
//
// <table-list.csv> uses the community listfile format ("fileDataID;path"); only
// dbfilesclient/*.db2 lines are read, so the full listfile works as well as the
// short list beside this script.
//
// Prints one JSON line per table, then a final {"done": ...} line.
//
// Unlike the other headless scripts this one lives outside the wow.export
// checkout and loads its modules by absolute path. Run it with node: bun 1.3
// fails to load wow.export's CASC modules ("Expected CommonJS module to have a
// function wrapper").

const path = require('path');
const fs = require('fs');
const os = require('os');
const Module = require('module');

const args = process.argv.slice(2);
const allowCdn = !args.includes('--no-cdn');
const [wowexportDir, installDir, product, listPath, outDir] = args.filter(a => !a.startsWith('--'));
if (!wowexportDir || !installDir || !product || !listPath || !outDir) {
	console.error('usage: node dump_db2.js <wowexport-checkout> <install-dir> <product> <table-list.csv> <out-dir> [--no-cdn]');
	process.exit(2);
}

const SRC = path.join(path.resolve(wowexportDir), 'src');

global.BUILD_RELEASE = true;
process.env.BUILD_RELEASE = 'true';

const DATA_PATH = path.join(os.tmpdir(), 'wowexport-headless');
fs.mkdirSync(DATA_PATH, { recursive: true });

// wow.export loads its native mmap addon from beside the running executable,
// which here is node. Point it at a real copy instead.
const MMAP_NODE = process.env.WOWEXPORT_MMAP_NODE || path.join(SRC, 'mmap.node');
if (!fs.existsSync(MMAP_NODE)) {
	console.error(`mmap.node not found at ${MMAP_NODE}`);
	console.error('Set WOWEXPORT_MMAP_NODE to the mmap.node in your wow.export install.');
	process.exit(1);
}
const _origLoad = Module._load;
Module._load = function (request, parent, isMain) {
	if (request.endsWith('mmap.node'))
		return _origLoad.call(this, MMAP_NODE, parent, isMain);
	return _origLoad.call(this, request, parent, isMain);
};

// NW.js globals the app modules touch at load time.
global.nw = {
	__dirname: SRC,
	App: {
		dataPath: DATA_PATH,
		manifest: { version: '0.0.0-headless' },
		argv: [],
	},
	Shell: { openItem: () => {}, openExternal: () => {} },
	Window: { get: () => ({ on: () => {}, showDevTools: () => {} }) },
};

const default_config = JSON.parse(
	fs.readFileSync(path.join(SRC, 'default_config.jsonc'), 'utf8').replace(/^\s*\/\/.*$/gm, '')
);

const core = require(path.join(SRC, 'js', 'core'));
core.view = core.view || {};
Object.assign(core.view, {
	config: Object.assign({}, default_config),
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

const CASCLocal = require(path.join(SRC, 'js', 'casc', 'casc-source-local'));
const BuildCache = require(path.join(SRC, 'js', 'casc', 'build-cache'));
const locale = require(path.join(SRC, 'js', 'casc', 'locale-flags'));

function readTableList(file) {
	const tables = [];
	for (const line of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
		const match = /^(\d+);dbfilesclient\/([^/]+)\.db2$/i.exec(line.trim());
		if (match)
			tables.push({ fdid: Number(match[1]), name: match[2].toLowerCase() });
	}
	return tables;
}

// Read a file through BLTE in full. Blocks encrypted with a key we do not have
// are left zero-filled, which is what CASC Explorer does too: those blocks hold
// hotfix rows the client has not unlocked yet.
async function readFile(casc, fdid, useCdn) {
	let reader = await casc.getFile(fdid, false, true, useCdn);
	try {
		reader.processAllBlocks();
		return { data: reader.raw, zeroed: false };
	} catch (e) {
		if (!/missing decryption key/i.test(e.message))
			throw e;
	}
	reader = await casc.getFile(fdid, true, true, useCdn);
	reader.processAllBlocks();
	return { data: reader.raw, zeroed: true };
}

async function main() {
	const tables = readTableList(listPath);
	if (tables.length === 0) {
		console.error(`no dbfilesclient/*.db2 entries in ${listPath}`);
		process.exit(1);
	}

	const casc = new CASCLocal(installDir);
	core.view.casc = casc;
	casc.locale = locale.flags.enUS;
	await casc.init();

	const build = casc.builds.find(b => b.Product === product);
	if (!build) {
		console.error(`product ${product} not found. Available:`, casc.builds.map(b => b.Product));
		process.exit(1);
	}

	casc.build = build;
	casc.cache = new BuildCache(build.BuildKey);
	await casc.cache.init();
	await casc.loadConfigs();
	await casc.loadIndexes();
	await casc.loadEncoding();
	await casc.loadRoot();

	fs.mkdirSync(outDir, { recursive: true });

	let written = 0;
	for (const { fdid, name } of tables) {
		const result = { table: name, fdid };

		if (!casc.rootEntries.has(fdid)) {
			// Listed but not in this build: the table was retired.
			result.status = 'absent';
			console.log(JSON.stringify(result));
			continue;
		}

		try {
			let file;
			try {
				file = await readFile(casc, fdid, false);
				result.source = 'local';
			} catch (e) {
				if (!allowCdn)
					throw e;
				file = await readFile(casc, fdid, true);
				result.source = 'cdn';
				result.local_error = e.message;
			}
			fs.writeFileSync(path.join(outDir, name + '.db2'), file.data);
			result.status = 'ok';
			result.zeroed = file.zeroed;
			written++;
		} catch (e) {
			result.status = 'failed';
			result.error = e.message;
		}
		console.log(JSON.stringify(result));
	}

	console.log(JSON.stringify({ done: true, version: build.Version, written }));
}

main().catch(e => {
	console.error(e.stack || e);
	process.exit(1);
});
