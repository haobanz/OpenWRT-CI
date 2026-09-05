'use strict';

/*
 * Standalone LuCI view fixture. It exercises only the frontend request contract;
 * no router, account, or network API is used.
 */
const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const viewPath = process.argv[2] ||
	'vendor/game-accelerators/luci-app-biubiu-acc/htdocs/luci-static/resources/view/biubiu-acc/main.js';
const source = fs.readFileSync(viewPath, 'utf8');

function deferred() {
	let resolve;
	let reject;
	const promise = new Promise(function(done, fail) {
		resolve = done;
		reject = fail;
	});
	return { promise: promise, resolve: resolve, reject: reject };
}

function makeElement(tag, attrs, children) {
	const element = {
		tag: tag,
		attrs: attrs || {},
		children: [],
		value: attrs && attrs.value != null ? String(attrs.value) : '',
		parent: null,
		classList: {
			add: function() {},
			remove: function() {}
		},
		setAttribute: function(name, value) {
			this.attrs[name] = value;
			if (name === 'value')
				this.value = String(value);
		}
	};
	append(element, children);
	if (tag === 'select' && !element.value) {
		const selected = findAll(element, function(node) {
			return node.tag === 'option' && node.attrs.selected;
		})[0];
		if (selected)
			element.value = String(selected.attrs.value);
	}
	return element;
}

function append(parent, children) {
	if (children == null)
		return;
	if (Array.isArray(children)) {
		children.forEach(function(child) { append(parent, child); });
		return;
	}
	if (typeof children === 'object')
		children.parent = parent;
	parent.children.push(children);
}

function contains(root, target) {
	if (root === target)
		return true;
	return root.children.some(function(child) {
		return typeof child === 'object' && contains(child, target);
	});
}

function findAll(root, predicate) {
	const found = [];
	(function walk(node) {
		if (typeof node !== 'object')
			return;
		if (predicate(node))
			found.push(node);
		node.children.forEach(walk);
	})(root);
	return found;
}

function textOf(node) {
	if (typeof node !== 'object')
		return String(node);
	return node.children.map(textOf).join('');
}

function classHas(node, name) {
	return String(node.attrs['class'] || '').split(/\s+/).indexOf(name) !== -1;
}

function buttonWithText(root, text) {
	const node = findAll(root, function(item) {
		return item.tag === 'button' && textOf(item) === text;
	})[0];
	assert(node, 'missing button: ' + text);
	return node;
}

function click(node) {
	assert(typeof node.attrs.click === 'function', 'button has no click handler: ' + textOf(node));
	return Promise.resolve(node.attrs.click({ currentTarget: node }));
}

async function flush() {
	for (let index = 0; index < 12; index++)
		await Promise.resolve();
	await new Promise(function(done) { setImmediate(done); });
	for (let index = 0; index < 12; index++)
		await Promise.resolve();
}

const document = {
	body: makeElement('body', {}, []),
	querySelector: function(selector) {
		return selector === '.modal' && modal ? modal : null;
	}
};
document.body.contains = function(target) {
	return contains(this, target);
};
let modal = null;
const writes = [];
let initialSearch = deferred();
const pendingInitialSearch = initialSearch;
let delayedOptions = null;
let failNextSave = false;
let delayedSave = null;
const config = {
	scope: 'lan',
	selected_games: [ 'steam', 'counter-strike-2', 'epic' ],
	target_name: '',
	target_ip: '',
	target_mac: '',
	target_id: '',
	area_id: '',
	platform_id: '',
	game_name: '',
	area_name: '',
	acc_mode: '',
	log_level: 'info',
	openclash_mode: 'exclusive'
};

function response(value) {
	return Promise.resolve({ code: 0, stdout: JSON.stringify(value), stderr: '' });
}

function searchResult(payload) {
	if (payload.operation === 'game_list' && initialSearch) {
		const pending = initialSearch;
		initialSearch = null;
		return pending.promise.then(function(value) { return { code: 0, stdout: JSON.stringify(value), stderr: '' }; });
	}
	if (payload.operation === 'game_list')
		return response({ success: true, games: [], has_next_page: false });
	if (payload.keyword === 'fail')
		return response({ success: false, message: '目录暂不可用' });
	if (payload.keyword === 'empty')
		return response({ success: true, games: [], has_next_page: false });
	if (payload.keyword === 'Page') {
		if (payload.page === 2)
			return response({ success: true, games: [ { id: '60000', name: 'Page Two', platform_id: '6' } ], has_next_page: false });
		return response({ success: true, games: [ { id: '59999', name: 'Page One', platform_id: '6' } ], has_next_page: true });
	}
	return response({
		success: true,
		games: [
			{ id: '38780', name: 'Counter-Strike 2', platform_id: '6' },
			{ id: '50000', name: 'Delayed Game', platform_id: '6' }
		],
		has_next_page: true
	});
}

const mockFs = {
	write: function(path, value) {
		assert.strictEqual(path, '/tmp/biubiu-acc/request.json');
		writes.push(JSON.parse(value));
		return Promise.resolve();
	},
	exec: function(command, args) {
		if (args[0] === 'snapshot')
			return response({ config: config, session: { authenticated: true }, acceleration_key: { cached: true }, capabilities: {}, data_plane: {}, traffic: {} });
		if (args[0] === 'catalog')
			return response({ profiles: [] });
		if (args[0] === 'match-status')
			return response({ available: false, flows: [] });
		assert.strictEqual(command, '/usr/libexec/biubiu-acc-manager');
		assert.strictEqual(args[0], 'request');
		const payload = writes[writes.length - 1];
		if (payload.operation === 'game_search' || payload.operation === 'game_list')
			return searchResult(payload);
		if (payload.operation === 'game_options' && payload.game_id === '50000') {
			delayedOptions = deferred();
			return delayedOptions.promise.then(function(value) {
				return { code: 0, stdout: JSON.stringify(value), stderr: '' };
			});
		}
		if (payload.operation === 'game_options') {
			return response({
				success: true,
				game: {
					id: '38780', name: 'Counter-Strike 2', platform_id: '6',
					areas: [ { id: '146', name: '智能全区服' }, { id: '147', name: '华东' } ],
					modes: [ { id: '3', name: '进程模式' }, { id: '5', name: '路由模式' } ]
				}
			});
		}
		if (payload.operation === 'save_config') {
			if (failNextSave) {
				failNextSave = false;
				return response({ success: false, message: '保存失败' });
			}
			Object.keys(config).forEach(function(key) { config[key] = payload[key]; });
			if (delayedSave)
				return delayedSave.promise.then(function() { return response({ success: true }); });
			return response({ success: true, message: '已保存' });
		}
		return response({ success: true });
	}
};

const ui = {
	showModal: function(title, children) {
		modal = makeElement('div', { 'class': 'modal', title: title }, children);
		append(document.body, modal);
	},
	hideModal: function() {
		if (modal) {
			document.body.children = document.body.children.filter(function(child) { return child !== modal; });
			modal = null;
		}
	},
	addNotification: function() {}
};

const context = {
	console: console,
	Promise: Promise,
	Error: Error,
	JSON: JSON,
	String: String,
	Number: Number,
	Array: Array,
	Object: Object,
	Math: Math,
	isFinite: isFinite,
	document: document,
	_: function(value) { return value; },
	E: makeElement,
	L: {
		resolveDefault: function(value, fallback) { return Promise.resolve(value).catch(function() { return fallback; }); },
		naturalCompare: function(left, right) { return String(left).localeCompare(String(right)); },
		resource: function(value) { return value; }
	},
	fs: mockFs,
	network: { getHostHints: function() { return Promise.resolve(null); } },
	poll: { add: function() {} },
	rpc: { declare: function() { return function() { return Promise.resolve({ dhcp_leases: [] }); }; } },
	ui: ui,
	dom: { content: function(node, children) { node.children = []; append(node, children); } },
	view: { extend: function(value) { return value; } }
};

const moduleFactory = vm.runInNewContext(
	'(function() { String.prototype.format = function() { var args = arguments; var index = 0; return this.replace(/%[ds]/g, function() { return args[index++]; }); };\n' + source + '\n})',
	context,
	{ filename: viewPath }
);
const luciView = moduleFactory();

async function main() {
	await luciView.load();
	const app = luciView.render();
	await click(buttonWithText(app, '加速配置'));
	await click(buttonWithText(app, '选择官方游戏'));
	await flush();
	assert(textOf(modal).includes('正在读取官方游戏目录…'), 'loading state is visible');
	pendingInitialSearch.resolve({ success: true, games: [], has_next_page: false });
	await flush();
	assert(textOf(modal).includes('没有找到官方游戏'), 'empty game_list keeps search available');

	let search = findAll(modal, function(node) { return node.tag === 'input' && node.attrs.type === 'search'; })[0];
	search.value = 'fail';
	await click(buttonWithText(modal, '搜索'));
	await flush();
	assert(textOf(modal).includes('目录暂不可用'), 'failure state is visible');
	buttonWithText(modal, '重试');

	search = findAll(modal, function(node) { return node.tag === 'input' && node.attrs.type === 'search'; })[0];
	search.value = 'empty';
	await click(buttonWithText(modal, '搜索'));
	await flush();
	assert(textOf(modal).includes('没有找到官方游戏'), 'empty state is visible');

	search = findAll(modal, function(node) { return node.tag === 'input' && node.attrs.type === 'search'; })[0];
	search.value = 'Page';
	await click(buttonWithText(modal, '搜索'));
	await flush();
	assert(textOf(modal).includes('Page One'), 'search result is visible');
	await click(buttonWithText(modal, '下一页'));
	await flush();
	assert(textOf(modal).includes('Page Two'), 'pagination uses the requested page');
	await click(buttonWithText(modal, '上一页'));
	await flush();
	assert(textOf(modal).includes('Page One'), 'previous page restores the search page');

	search = findAll(modal, function(node) { return node.tag === 'input' && node.attrs.type === 'search'; })[0];
	search.value = 'Counter';
	await click(buttonWithText(modal, '搜索'));
	await flush();

	await click(findAll(modal, function(node) {
		return node.tag === 'button' && classHas(node, 'bba-provider-game') && textOf(node).includes('Delayed Game');
	})[0]);
	await flush();
	assert(textOf(modal).includes('正在读取区服和模式…'), 'options loading state is visible');
	await click(buttonWithText(modal, '返回游戏列表'));
	await click(findAll(modal, function(node) {
		return node.tag === 'button' && classHas(node, 'bba-provider-game') && textOf(node).includes('Counter-Strike 2');
	})[0]);
	const firstDelayedOptions = delayedOptions;
	firstDelayedOptions.resolve({ success: true, game: {
		id: '50000', name: 'Delayed Game', platform_id: '6',
		areas: [ { id: '1', name: '旧区服' } ], modes: []
	} });
	await flush();
	assert(textOf(modal).includes('智能全区服'), 'newer option request wins: ' + textOf(modal));
	assert(textOf(modal).includes('Counter-Strike 2'), 'stale option response is ignored');

	const selects = findAll(modal, function(node) { return node.tag === 'select'; });
	assert.strictEqual(selects.length, 2, 'area and mode controls render');
	selects[0].value = '147';
	selects[1].value = '3';
	selects[0].onchange();
	selects[1].onchange();
	failNextSave = true;
	await click(buttonWithText(modal, '保存加速选择'));
	await flush();
	const retrySelects = findAll(modal, function(node) { return node.tag === 'select'; });
	assert.strictEqual(retrySelects[0].value, '147', 'failed save keeps the selected area');
	assert.strictEqual(retrySelects[1].value, '3', 'failed save keeps the selected mode');
	await click(buttonWithText(modal, '保存加速选择'));
	await flush();
	const saves = writes.filter(function(payload) { return payload.operation === 'save_config'; });
	assert.strictEqual(saves.length, 2, 'only an explicit retry sends a second save');
	assert.deepStrictEqual(saves[1], {
		operation: 'save_config', scope: 'lan', selected_games: [ 'steam', 'counter-strike-2', 'epic' ],
		target_name: '', target_ip: '', target_mac: '', target_id: '38780', area_id: '147', platform_id: '6',
		game_name: 'Counter-Strike 2', area_name: '华东', acc_mode: '3', log_level: 'info', openclash_mode: 'exclusive'
	});

	await click(buttonWithText(app, '选择官方游戏'));
	await flush();
	search = findAll(modal, function(node) { return node.tag === 'input' && node.attrs.type === 'search'; })[0];
	search.value = 'Counter';
	await click(buttonWithText(modal, '搜索'));
	await flush();
	await click(findAll(modal, function(node) {
		return node.tag === 'button' && classHas(node, 'bba-provider-game') && textOf(node).includes('Counter-Strike 2');
	})[0]);
	await flush();
	const reopenedSelects = findAll(modal, function(node) { return node.tag === 'select'; });
	assert.strictEqual(reopenedSelects[0].value, '147', 'saved area is restored when still valid');
	assert.strictEqual(reopenedSelects[1].value, '3', 'saved mode is restored when still valid');
	delayedSave = deferred();
	const saving = click(buttonWithText(modal, '保存加速选择'));
	await flush();
	ui.hideModal();
	await click(buttonWithText(app, '选择官方游戏'));
	const newerModal = modal;
	delayedSave.resolve();
	await saving;
	await flush();
	delayedSave = null;
	assert.strictEqual(modal, newerModal, 'a late save cannot close a newer picker');
	await click(buttonWithText(modal, '取消'));

	await click(buttonWithText(app, '选择官方游戏'));
	await flush();
	search = findAll(modal, function(node) { return node.tag === 'input' && node.attrs.type === 'search'; })[0];
	search.value = 'Counter';
	await click(buttonWithText(modal, '搜索'));
	await flush();
	await click(findAll(modal, function(node) {
		return node.tag === 'button' && classHas(node, 'bba-provider-game') && textOf(node).includes('Delayed Game');
	})[0]);
	await flush();
	await click(buttonWithText(modal, '取消'));
	const cancelled = delayedOptions;
	cancelled.resolve({ success: true, game: {
		id: '50000', name: 'Delayed Game', platform_id: '6', areas: [ { id: '1', name: '旧区服' } ], modes: []
	} });
	await flush();
	assert.strictEqual(modal, null, 'cancelled modal is not updated by an in-flight request');

	console.log('biubiu LuCI official picker fixture: PASS');
}

main().catch(function(err) {
	console.error(err.stack || err);
	process.exitCode = 1;
});
