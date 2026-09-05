'use strict';

/* Browser-only fixture for the LuCI official-game modal. No router API is called. */
const assert = require('assert');
const fs = require('fs');
const { chromium } = require('playwright');

const root = process.argv[2] || process.cwd();
const viewPath = root + '/vendor/game-accelerators/luci-app-biubiu-acc/htdocs/luci-static/resources/view/biubiu-acc/main.js';
const cssPath = root + '/vendor/game-accelerators/luci-app-biubiu-acc/htdocs/luci-static/resources/view/biubiu-acc/main.css';
const source = fs.readFileSync(viewPath, 'utf8');
const css = fs.readFileSync(cssPath, 'utf8');

async function assertFits(page, width) {
	const metrics = await page.evaluate(function() {
		const modal = document.querySelector('.modal');
		const bounds = modal.getBoundingClientRect();
		return {
			scrollWidth: document.documentElement.scrollWidth,
			viewport: window.innerWidth,
			left: bounds.left,
			right: bounds.right
		};
	});
	assert(metrics.scrollWidth <= width, 'document overflows at ' + width + 'px');
	assert(metrics.left >= 0 && metrics.right <= width, 'modal is clipped at ' + width + 'px');
}

async function main() {
	const browser = await chromium.launch({
		headless: true,
		executablePath: '/opt/google/chrome/google-chrome',
		args: [ '--no-sandbox' ]
	});
	const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
	try {
		await page.setContent('<main id="app"></main>');
		await page.addStyleTag({ content: [
			'body { box-sizing: border-box; margin: 0; padding: 24px; font: 14px sans-serif; }',
			'*, *::before, *::after { box-sizing: border-box; }',
			'.modal { position: fixed; z-index: 10; left: 50%; top: 24px; width: min(720px, calc(100vw - 32px)); max-height: calc(100vh - 48px); overflow: auto; padding: 16px; border: 1px solid #d9dee5; background: #fff; transform: translateX(-50%); }',
			'.cbi-button { min-height: 36px; }',
			css
		].join('\n') });
		await page.evaluate(async function(viewSource) {
			function append(node, children) {
				if (children == null)
					return;
				if (Array.isArray(children)) {
					children.forEach(function(child) { append(node, child); });
					return;
				}
				node.appendChild(children instanceof Node ? children : document.createTextNode(String(children)));
			}
			window._ = function(value) { return value; };
			window.E = function(tag, attrs, children) {
				const node = document.createElement(tag);
				Object.keys(attrs || {}).forEach(function(key) {
					const value = attrs[key];
					if (value == null)
						return;
					if (key === 'class')
						node.className = value;
					else if (key === 'click')
						node.addEventListener('click', value);
					else if (key === 'value')
						node.value = value;
					else if (key === 'checked' || key === 'disabled')
						node[key] = !!value;
					else
						node.setAttribute(key, value);
				});
				append(node, children);
				return node;
			};
			const config = {
				scope: 'lan', selected_games: [ 'steam', 'counter-strike-2', 'epic' ],
				target_name: '', target_ip: '', target_mac: '', target_id: '', area_id: '', platform_id: '',
				game_name: '', area_name: '', acc_mode: '', log_level: 'info', openclash_mode: 'exclusive'
			};
			window.fixtureWrites = [];
			let activeModal = null;
			window.fs = {
				write: function(path, value) {
					if (path !== '/tmp/biubiu-acc/request.json')
						throw new Error('unexpected path');
					window.fixtureWrites.push(JSON.parse(value));
					return Promise.resolve();
				},
				exec: function(command, args) {
					const operation = args[0];
					const result = function(value) { return Promise.resolve({ code: 0, stdout: JSON.stringify(value), stderr: '' }); };
					if (operation === 'snapshot')
						return result({ config: config, session: { authenticated: true }, acceleration_key: { cached: true }, capabilities: {}, data_plane: {}, traffic: {} });
					if (operation === 'catalog')
						return result({ profiles: [] });
					if (operation === 'match-status')
						return result({ available: false, flows: [] });
					if (command !== '/usr/libexec/biubiu-acc-manager' || operation !== 'request')
						throw new Error('unexpected manager request');
					const request = window.fixtureWrites[window.fixtureWrites.length - 1];
					if (request.operation === 'game_list')
						return result({ success: true, games: [], has_next_page: false });
					if (request.operation === 'game_search') {
						return result(request.keyword === 'Counter' ? {
							success: true, games: [ { id: '38780', name: 'Counter-Strike 2 <safe>', platform_id: '6' } ], has_next_page: false
						} : { success: true, games: [], has_next_page: false });
					}
					if (request.operation === 'game_options') {
						return result({ success: true, game: {
							id: '38780', name: 'Counter-Strike 2 <safe>', platform_id: '6',
							areas: [ { id: '146', name: '智能全区服' }, { id: '147', name: '华东' } ],
							modes: [ { id: '3', name: '进程模式' }, { id: '5', name: '路由模式' } ]
						} });
					}
					if (request.operation === 'save_config') {
						Object.keys(config).forEach(function(key) { config[key] = request[key]; });
						return result({ success: true, message: '已保存' });
					}
					return result({ success: true });
				}
			};
			window.network = { getHostHints: function() { return Promise.resolve(null); } };
			window.poll = { add: function() {} };
			window.rpc = { declare: function() { return function() { return Promise.resolve({ dhcp_leases: [] }); }; } };
			window.ui = {
				showModal: function(title, children) {
					activeModal = E('section', { 'class': 'modal', 'aria-label': title }, children);
					document.body.appendChild(activeModal);
				},
				hideModal: function() {
					if (activeModal)
						activeModal.remove();
					activeModal = null;
				},
				addNotification: function() {}
			};
			window.dom = { content: function(node, children) { node.replaceChildren(); append(node, children); } };
			window.L = {
				resolveDefault: function(value, fallback) { return Promise.resolve(value).catch(function() { return fallback; }); },
				naturalCompare: function(left, right) { return String(left).localeCompare(String(right)); },
				resource: function(value) { return value; }
			};
			window.view = { extend: function(value) { return value; } };
			String.prototype.format = function() {
				let index = 0;
				const values = arguments;
				return this.replace(/%[ds]/g, function() { return values[index++]; });
			};
			const luciView = new Function(viewSource)();
			await luciView.load();
			document.getElementById('app').appendChild(luciView.render());
		}, source);

		await page.getByRole('button', { name: '加速配置' }).click();
		await page.getByRole('button', { name: '选择官方游戏' }).click();
		await page.getByText('没有找到官方游戏').waitFor();
		await page.getByPlaceholder('搜索官方游戏').fill('Counter');
		await page.getByRole('button', { name: '搜索' }).click();
		await page.locator('.bba-provider-game').click();
		await page.locator('.bba-picker-selected').waitFor();
		assert.strictEqual(await page.locator('.bba-provider-game img').count(), 0, 'game name is rendered as text');
		assert((await page.locator('.bba-picker-selected').textContent()).includes('<safe>'), 'escaped name stays visible as text');
		await assertFits(page, 1280);
		await page.screenshot({ path: '/tmp/biubiu-picker-desktop.png', fullPage: true });

		await page.setViewportSize({ width: 390, height: 844 });
		await assertFits(page, 390);
		await page.screenshot({ path: '/tmp/biubiu-picker-mobile.png', fullPage: true });
		await page.locator('select').nth(0).selectOption('147');
		await page.locator('select').nth(1).selectOption('3');
		await page.getByRole('button', { name: '保存加速选择' }).click();
		await page.locator('.modal').waitFor({ state: 'detached' });
		const save = await page.evaluate(function() {
			return window.fixtureWrites.filter(function(item) { return item.operation === 'save_config'; })[0];
		});
		assert.deepStrictEqual(save, {
			operation: 'save_config', scope: 'lan', selected_games: [ 'steam', 'counter-strike-2', 'epic' ],
			target_name: '', target_ip: '', target_mac: '', target_id: '38780', area_id: '147', platform_id: '6',
			game_name: 'Counter-Strike 2 <safe>', area_name: '华东', acc_mode: '3', log_level: 'info', openclash_mode: 'exclusive'
		});
		await page.getByText('华东', { exact: true }).waitFor();
		await page.screenshot({ path: '/tmp/biubiu-picker-saved-mobile.png', fullPage: true });

		await page.getByRole('button', { name: '选择官方游戏' }).click();
		await page.getByText('没有找到官方游戏').waitFor();
		await page.getByPlaceholder('搜索官方游戏').fill('Counter');
		await page.getByRole('button', { name: '搜索' }).click();
		await page.locator('.bba-provider-game').click();
		await page.locator('.bba-picker-selected').waitFor();
		assert.strictEqual(await page.locator('select').nth(0).inputValue(), '147', 'saved area is restored');
		assert.strictEqual(await page.locator('select').nth(1).inputValue(), '3', 'saved mode is restored');
		await assertFits(page, 390);
		await page.screenshot({ path: '/tmp/biubiu-picker-restored-mobile.png', fullPage: true });
		console.log('biubiu LuCI Playwright desktop/mobile fixture: PASS');
	} finally {
		await browser.close();
	}
}

main().catch(function(err) {
	console.error(err.stack || err);
	process.exitCode = 1;
});
