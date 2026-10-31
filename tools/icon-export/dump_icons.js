// Exports icon textures out of a local WoW install as PNG, using wow.export's
// CASC layer and its own BLP decoder, so no window or manual export is needed.
// export_icons.py runs this; it can also be run on its own.
//
// Run:  node dump_icons.js <wowexport-checkout> <install-dir> <product> <icon-list.csv> <out-dir> [--no-cdn]
//
// <icon-list.csv> uses the community listfile format ("fileDataID;path"); only
// interface/icons/*.blp lines are read, so the full listfile works as well as
// the list export_icons.py writes. Each icon is saved as its lowercase file
// name with .png in place of .blp, which is the name the app looks up.
//
// Prints one JSON line per icon that was not written, a progress line every
// 2000 icons, then a final {"done": ...} line.
//
// Like dump_db2.js this lives outside the wow.export checkout and loads its
// modules by absolute path. Run it with node, not bun.

const path = require('path');
const fs = require('fs');
const os = require('os');
const Module = require('module');

const args = process.argv.slice(2);
const allowCdn = !args.includes('--no-cdn');
const [wowexportDir, installDir, product, listPath, outDir] = args.filter(a => !a.startsWith('--'));
if (!wowexportDir || !installDir || !product || !listPath || !outDir) {
	console.error('usage: node dump_icons.js <wowexport-checkout> <install-dir> <product> <icon-list.csv> <out-dir> [--no-cdn]');
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
const BLPImage = require(path.join(SRC, 'js', 'casc', 'blp'));
const locale = require(path.join(SRC, 'js', 'casc', 'locale-flags'));

function readIconList(file) {
	const icons = new Map();
	for (const line of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
		const match = /^(\d+);interface[\\/]icons[\\/]([^\\/]+)\.blp$/i.exec(line.trim());
		if (match)
			icons.set(Number(match[1]), match[2].toLowerCase() + '.png');
	}
	return icons;
}

async function main() {
	const icons = readIconList(listPath);
	if (icons.size === 0) {
		console.error(`no interface/icons/*.blp entries in ${listPath}`);
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

	let written = 0, absent = 0, fromCdn = 0, done = 0;
	for (const [fdid, name] of icons) {
		done++;
		if (done % 2000 === 0)
			console.log(JSON.stringify({ progress: done, total: icons.size }));

		if (!casc.rootEntries.has(fdid)) {
			// Listed but not in this build: the icon was removed. Icons present
			// for other locales only are counted the same way below.
			absent++;
			continue;
		}

		try {
			let file;
			try {
				file = await casc.getFile(fdid, false, true, false);
			} catch (e) {
				if (!allowCdn || /missing decryption key/i.test(e.message))
					throw e;
				file = await casc.getFile(fdid, false, true, true);
				fromCdn++;
				console.log(JSON.stringify({ icon: name, fdid, status: 'cdn', local_error: e.message }));
			}
			const png = new BLPImage(file).toPNG();
			await png.writeToFile(path.join(outDir, name));
			written++;
		} catch (e) {
			if (/no root entry found for locale/i.test(e.message)) {
				// In the root, but only for other locales: not part of this client.
				absent++;
				continue;
			}
			const status = /missing decryption key/i.test(e.message) ? 'encrypted' : 'failed';
			console.log(JSON.stringify({ icon: name, fdid, status, error: e.message }));
		}
	}

	console.log(JSON.stringify({ done: true, version: build.Version, listed: icons.size, written, absent, from_cdn: fromCdn }));
}

main().catch(e => {
	console.error(e.stack || e);
	process.exit(1);
});
