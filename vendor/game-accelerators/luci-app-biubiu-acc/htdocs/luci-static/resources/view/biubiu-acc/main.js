'use strict';
'require dom';
'require fs';
'require network';
'require poll';
'require rpc';
'require ui';
'require view';

const COMMAND = '/usr/libexec/biubiu-acc-manager';
const REQUEST_FILE = '/tmp/biubiu-acc/request.json';
const TABS = [
	[ 'overview', _('状态') ],
	[ 'account', _('账号') ],
	[ 'devices', _('设备') ],
	[ 'acceleration', _('加速配置') ],
	[ 'logs', _('日志') ]
];

const callDHCPLeases = rpc.declare({
	object: 'luci-rpc',
	method: 'getDHCPLeases',
	expect: { '': {} }
});

let appNode = null;
let activeTab = 'overview';
let snapshot = { config: {}, session: {}, acceleration_key: {}, capabilities: {} };
let catalog = { profiles: [] };
let matchStatus = { available: false, flows: [] };
let devices = [];
let logText = '';

function commandError(res, fallback) {
	return new Error((res && (res.stderr || res.stdout) || fallback || _('操作失败')).trim());
}

function parseJSONCommand(res, fallback) {
	if (!res || res.code !== 0)
		throw commandError(res, fallback);
	try {
		return JSON.parse(res.stdout || '{}');
	}
	catch (err) {
		throw new Error(res.stderr || fallback || _('后端返回了无效数据'));
	}
}

function getSnapshot() {
	return fs.exec(COMMAND, [ 'snapshot' ]).then(function(res) {
		return parseJSONCommand(res, _('无法读取 biubiu 状态'));
	});
}

function getCatalog() {
	return fs.exec(COMMAND, [ 'catalog' ]).then(function(res) {
		const result = parseJSONCommand(res, _('无法读取内置游戏目录'));
		result.profiles = Array.isArray(result.profiles) ? result.profiles : [];
		return result;
	});
}

function getMatchStatus() {
	return fs.exec(COMMAND, [ 'match-status' ]).then(function(res) {
		const result = parseJSONCommand(res, _('无法读取实时匹配状态'));
		result.flows = Array.isArray(result.flows) ? result.flows : [];
		return result;
	});
}

function getDevices() {
	return Promise.all([
		L.resolveDefault(callDHCPLeases(), {}),
		L.resolveDefault(network.getHostHints(), null)
	]).then(function(data) {
		const leases = Array.isArray(data[0].dhcp_leases) ? data[0].dhcp_leases : [];
		const hints = data[1];
		const found = Object.create(null);

		leases.forEach(function(lease) {
			if (!lease.ipaddr)
				return;
			const mac = String(lease.macaddr || '').toLowerCase();
			const key = mac || lease.ipaddr;
			found[key] = {
				name: lease.hostname || (hints && hints.getHostnameByMACAddr(mac)) || '',
				mac: mac,
				ip: lease.ipaddr,
				active: true
			};
		});
		if (hints) {
			hints.getMACHints(false).forEach(function(item) {
				const mac = String(item[0] || '').toLowerCase();
				const ip = hints.getIPAddrByMACAddr(mac);
				if (!ip || found[mac])
					return;
				found[mac] = {
					name: hints.getHostnameByMACAddr(mac) || item[1] || '',
					mac: mac,
					ip: ip,
					active: false
				};
			});
		}
		devices = Object.keys(found).map(function(key) { return found[key]; });
		devices.sort(function(a, b) {
			if (a.active !== b.active)
				return a.active ? -1 : 1;
			return L.naturalCompare(a.name || a.ip, b.name || b.ip);
		});
		return devices;
	});
}

function notifyError(err) {
	ui.addNotification(null, E('p', {}, err && err.message || String(err)), 'error');
}

function refreshView() {
	return Promise.all([
		getSnapshot(),
		L.resolveDefault(getMatchStatus(), matchStatus)
	]).then(function(data) {
		snapshot = data[0];
		matchStatus = data[1];
		if (appNode)
			dom.content(appNode, renderPage());
	});
}

function runRequest(payload, options) {
	options = options || {};
	return fs.write(REQUEST_FILE, JSON.stringify(payload), 384).then(function() {
		return fs.exec(COMMAND, [ 'request' ]);
	}).then(function(res) {
		const result = parseJSONCommand(res, _('后端操作失败'));
		if (result.success === false)
			throw new Error(result.message || _('后端操作失败'));
		if (options.notify !== false)
			ui.addNotification(null, E('p', {}, result.message || _('操作完成')), 'info');
		return options.refresh === false ? result : refreshView().then(function() { return result; });
	});
}

function saveConfig(changes) {
	const current = snapshot.config || {};
	const payload = {
		operation: 'save_config',
		scope: current.scope || 'lan',
		selected_games: Array.isArray(current.selected_games) ? current.selected_games.slice() : [],
		target_name: current.target_name || '',
		target_ip: current.target_ip || '',
		target_mac: current.target_mac || '',
		target_id: current.target_id || '',
		area_id: current.area_id || '',
		platform_id: current.platform_id || '',
		log_level: current.log_level || 'info',
		openclash_mode: current.openclash_mode || 'exclusive'
	};
	Object.keys(changes || {}).forEach(function(key) { payload[key] = changes[key]; });
	return runRequest(payload);
}

function button(label, style, handler, disabled, title) {
	const attributes = {
		type: 'button',
		'class': 'cbi-button ' + (style || 'cbi-button-neutral'),
		disabled: disabled ? 'disabled' : null,
		title: title || null
	};
	if (handler)
		attributes.click = handler;
	return E('button', attributes, label);
}

function withBusy(ev, task) {
	const target = ev && ev.currentTarget;
	if (target) {
		target.disabled = true;
		target.classList.add('spinning');
	}
	return Promise.resolve().then(task).catch(notifyError).finally(function() {
		if (target && document.body.contains(target)) {
			target.disabled = false;
			target.classList.remove('spinning');
		}
	});
}

function input(type, value, placeholder) {
	return E('input', {
		type: type || 'text',
		value: value == null ? '' : value,
		placeholder: placeholder || '',
		autocomplete: type === 'password' ? 'new-password' : null
	});
}

function select(options, value) {
	return E('select', {}, options.map(function(item) {
		return E('option', {
			value: item[0],
			selected: String(item[0]) === String(value) ? 'selected' : null,
			disabled: item[2] ? 'disabled' : null
		}, item[1]);
	}));
}

function fieldRows(rows) {
	const children = [];
	rows.forEach(function(row) { children.push(E('label', {}, row[0]), row[1]); });
	return E('div', { 'class': 'bba-form' }, children);
}

function modalActions(actions) {
	return E('div', { 'class': 'right bba-actions' }, actions);
}

function badge(label, kind) {
	return E('span', { 'class': 'bba-badge bba-' + kind }, label);
}

function table(headers, rows) {
	return E('div', { 'class': 'bba-table-wrap' }, E('table', { 'class': 'bba-table' }, [
		E('tr', {}, headers.map(function(header) { return E('th', {}, header); }))
	].concat(rows)));
}

function section(title, actions, content) {
	return E('section', { 'class': 'bba-section' }, [
		E('div', { 'class': 'bba-section-head' }, [
			E('h3', {}, title),
			E('div', { 'class': 'bba-actions' }, actions || [])
		]),
		content
	]);
}

function formatDuration(value) {
	let seconds = Math.max(0, Math.floor(Number(value) || 0));
	const hours = Math.floor(seconds / 3600);
	const minutes = Math.floor(seconds % 3600 / 60);
	if (hours)
		return '%d 小时 %d 分'.format(hours, minutes);
	if (minutes)
		return '%d 分 %d 秒'.format(minutes, seconds % 60);
	return '%d 秒'.format(seconds);
}

function formatNumber(value) {
	const number = Number(value);
	return isFinite(number) && number >= 0 ? String(Math.floor(number)) : '0';
}

function formatBytes(value) {
	const number = Number(value);
	if (!isFinite(number) || number < 0)
		return '0 B';
	if (number >= 1024 * 1024 * 1024)
		return (number / (1024 * 1024 * 1024)).toFixed(1) + ' GiB';
	if (number >= 1024 * 1024)
		return (number / (1024 * 1024)).toFixed(1) + ' MiB';
	if (number >= 1024)
		return (number / 1024).toFixed(1) + ' KiB';
	return formatNumber(number) + ' B';
}

function phaseBadge() {
	if (snapshot.accelerating)
		return badge(_('加速中'), 'ok');
	if (snapshot.phase === 'ready')
		return badge(_('已就绪'), 'ok');
	if (snapshot.phase === 'traffic_error')
		return badge(_('接管异常'), 'warn');
	return badge(snapshot.phase_message || _('等待配置'), 'idle');
}

function serviceAction(action) {
	return runRequest({ operation: 'service_action', action: action });
}

function showLoginModal() {
	const area = input('text', '86', '86');
	const phone = input('tel', '', _('手机号'));
	const code = input('text', '', _('短信验证码'));
	code.setAttribute('inputmode', 'numeric');
	code.setAttribute('autocomplete', 'one-time-code');
	ui.showModal(_('手机号登录'), [
		fieldRows([
			[ _('国家/地区代码'), area ],
			[ _('手机号'), phone ],
			[ _('验证码'), code ]
		]),
		modalActions([
			button(_('取消'), 'cbi-button-neutral', ui.hideModal),
			button(_('发送验证码'), 'cbi-button-neutral', function(ev) {
				return withBusy(ev, function() {
					return runRequest({ operation: 'sms_send', area_code: area.value.trim(), phone: phone.value.trim() }, { refresh: false });
				});
			}),
			button(_('登录'), 'cbi-button-positive', function(ev) {
				return withBusy(ev, function() {
					return runRequest({
						operation: 'sms_login', area_code: area.value.trim(),
						phone: phone.value.trim(), code: code.value.trim()
					}).then(ui.hideModal);
				});
			})
		])
	]);
}

function showKeyModal() {
	const value = E('textarea', {
		placeholder: 'VERSION|BASE64_X509_DER',
		autocomplete: 'off',
		spellcheck: 'false'
	});
	ui.showModal(_('导入加速业务公钥'), [
		fieldRows([ [ _('公钥值'), value ] ]),
		modalActions([
			button(_('取消'), 'cbi-button-neutral', ui.hideModal),
			button(_('验证并导入'), 'cbi-button-positive', function(ev) {
				return withBusy(ev, function() {
					return runRequest({ operation: 'import_key', value: value.value.trim() }).then(ui.hideModal);
				});
			})
		])
	]);
}

function showDeviceModal(device) {
	const name = input('text', device && device.name || '', _('设备名称'));
	const ip = input('text', device && device.ip || '', '192.168.100.175');
	const mac = input('text', device && device.mac || '', '00:11:22:33:44:55');
	ui.showModal(device ? _('选择加速设备') : _('手动添加设备'), [
		fieldRows([
			[ _('设备名称'), name ],
			[ _('IPv4 地址'), ip ],
			[ _('MAC 地址'), mac ]
		]),
		modalActions([
			button(_('取消'), 'cbi-button-neutral', ui.hideModal),
			button(_('保存'), 'cbi-button-positive', function(ev) {
				return withBusy(ev, function() {
					return saveConfig({
						scope: 'device',
						target_name: name.value.trim(), target_ip: ip.value.trim(),
						target_mac: mac.value.trim().toLowerCase()
					}).then(ui.hideModal);
				});
			})
		])
	]);
}

function profileMap() {
	const result = Object.create(null);
	(catalog.profiles || []).forEach(function(profile) { result[profile.id] = profile; });
	return result;
}

function showGameModal() {
	const current = snapshot.config || {};
	const selected = Object.create(null);
	const choices = [];
	(Array.isArray(current.selected_games) ? current.selected_games : []).forEach(function(id) {
		selected[id] = true;
	});
	const rows = (catalog.profiles || []).map(function(profile) {
		const checkbox = E('input', {
			type: 'checkbox',
			checked: selected[profile.id] ? 'checked' : null
		});
		choices.push({ id: profile.id, checkbox: checkbox });
		return E('label', { 'class': 'bba-game-option' }, [
			checkbox,
			E('span', {}, [
				E('strong', {}, profile.name || profile.id),
				E('small', {}, profile.description || '')
			])
		]);
	});
	ui.showModal(_('选择内置游戏'), [
		rows.length
			? E('div', { 'class': 'bba-game-grid' }, rows)
			: E('div', { 'class': 'bba-empty' }, _('内置游戏目录不可用')),
		modalActions([
			button(_('取消'), 'cbi-button-neutral', ui.hideModal),
			button(_('保存选择'), 'cbi-button-positive', function(ev) {
				return withBusy(ev, function() {
					return saveConfig({
						selected_games: choices.filter(function(choice) {
							return choice.checkbox.checked;
						}).map(function(choice) { return choice.id; })
					}).then(ui.hideModal);
				});
			})
		])
	]);
}

function showProfileModal() {
	const config = snapshot.config || {};
	const game = input('text', config.target_id || '', _('游戏 ID'));
	const area = input('text', config.area_id || '', _('区服 ID'));
	const platform = input('text', config.platform_id || '', _('平台 ID'));
	const logLevel = select([
		[ 'debug', 'Debug' ], [ 'info', 'Info' ], [ 'warn', 'Warn' ], [ 'error', 'Error' ]
	], config.log_level || 'info');
	const openclashMode = select([
		[ 'exclusive', _('独占：启动加速时自动停止 OpenClash') ]
	], config.openclash_mode || 'exclusive');
	game.setAttribute('inputmode', 'numeric');
	area.setAttribute('inputmode', 'numeric');
	platform.setAttribute('inputmode', 'numeric');
	ui.showModal(_('高级设置'), [
		fieldRows([
			[ _('游戏 ID'), game ],
			[ _('区服 ID'), area ],
			[ _('平台 ID'), platform ],
			[ _('日志级别'), logLevel ],
			[ _('OpenClash 冲突策略'), openclashMode ]
		]),
		modalActions([
			button(_('取消'), 'cbi-button-neutral', ui.hideModal),
			button(_('保存'), 'cbi-button-positive', function(ev) {
				return withBusy(ev, function() {
					return saveConfig({
						target_id: game.value.trim(), area_id: area.value.trim(),
						platform_id: platform.value.trim(), log_level: logLevel.value,
						openclash_mode: openclashMode.value
					}).then(ui.hideModal);
				});
			})
		])
	]);
}

function renderOverview() {
	const session = snapshot.session || {};
	const config = snapshot.config || {};
	const scopeText = config.scope === 'device'
		? (config.target_name || config.target_ip || _('未选择设备'))
		: _('整个局域网');
	const managerText = snapshot.manager_running
		? _('运行中') + (snapshot.rss_kb ? ' · ' + (snapshot.rss_kb / 1024).toFixed(1) + ' MB' : '')
		: (snapshot.manager_enabled ? _('启动异常') : _('已停止'));
	const capabilities = snapshot.capabilities || {};
	const capabilityRows = [
		[ _('账号登录与私有会话'), capabilities.account_login ],
		[ _('设备与配置存储'), capabilities.profile_storage ],
		[ _('内置游戏目录'), capabilities.builtin_catalog ],
		[ _('Bolt v3 帧编解码'), capabilities.bolt_v3_codec ],
		[ _('加速控制 API'), capabilities.control_api ],
		[ _('TCP/UDP 数据通道'), capabilities.data_channel ],
		[ _('自动游戏流量匹配'), capabilities.automatic_matching ],
		[ _('nftables 流量接管'), capabilities.traffic_steering ]
	].map(function(item) {
		return E('tr', {}, [ E('td', {}, item[0]), E('td', {}, item[1] ? badge(_('已实现'), 'ok') : badge(_('开发中'), 'warn')) ]);
	});
	return E('div', {}, [
		E('div', { 'class': 'bba-callout' }, snapshot.phase_message || _('等待配置')),
		E('div', { 'class': 'bba-stat-grid' }, [
			E('div', { 'class': 'bba-stat' }, [ E('span', { 'class': 'bba-muted' }, _('账号')), E('strong', {}, session.authenticated ? _('已登录') : _('未登录')) ]),
			E('div', { 'class': 'bba-stat' }, [ E('span', { 'class': 'bba-muted' }, _('加速范围')), E('strong', {}, scopeText) ]),
			E('div', { 'class': 'bba-stat' }, [ E('span', { 'class': 'bba-muted' }, _('管理服务')), E('strong', {}, managerText) ]),
			E('div', { 'class': 'bba-stat' }, [ E('span', { 'class': 'bba-muted' }, _('流量状态')), E('strong', {}, snapshot.accelerating ? _('已接管') : _('未接管')) ])
		]),
		section(_('实现进度'), [
			button(_('运行离线自检'), 'cbi-button-neutral', function(ev) {
				return withBusy(ev, function() { return runRequest({ operation: 'self_test' }); });
			})
		], table([ _('模块'), _('状态') ], capabilityRows)),
		snapshot.manager_running ? section(_('管理进程'), [], table([ 'PID', _('内存'), _('运行时间') ], [
			E('tr', {}, [ E('td', { 'class': 'bba-code' }, String(snapshot.pid || '-')), E('td', {}, snapshot.rss_kb ? (snapshot.rss_kb / 1024).toFixed(1) + ' MB' : '-'), E('td', {}, formatDuration(snapshot.uptime_seconds)) ])
		])) : null
	]);
}

function renderAccount() {
	const session = snapshot.session || {};
	const key = snapshot.acceleration_key || {};
	const fingerprint = key.fingerprint_sha256 ? key.fingerprint_sha256.slice(0, 16) + '…' : '-';
	return E('div', {}, [
		section(_('账号会话'), [
			button(_('手机号登录'), 'cbi-button-add', showLoginModal),
			button(_('续期'), 'cbi-button-neutral', function(ev) {
				return withBusy(ev, function() { return runRequest({ operation: 'session_refresh' }); });
			}, !session.refreshable),
			button(_('退出本机'), 'cbi-button-negative', function(ev) {
				return withBusy(ev, function() { return runRequest({ operation: 'session_clear' }); });
			}, !session.authenticated)
		], table([ _('项目'), _('状态') ], [
			E('tr', {}, [ E('td', {}, _('登录状态')), E('td', {}, session.authenticated ? badge(_('已登录'), 'ok') : badge(_('未登录'), 'idle')) ]),
			E('tr', {}, [ E('td', {}, _('登录方式')), E('td', {}, session.method || 'none') ]),
			E('tr', {}, [ E('td', {}, _('Cookie 数量')), E('td', {}, String(session.cookie_count || 0)) ])
		])),
		section(_('加速业务公钥'), [ button(key.cached ? _('替换') : _('导入'), 'cbi-button-neutral', showKeyModal) ], table([ _('项目'), _('状态') ], [
			E('tr', {}, [ E('td', {}, _('缓存状态')), E('td', {}, key.cached ? badge(_('已验证'), 'ok') : badge(_('未配置'), 'warn')) ]),
			E('tr', {}, [ E('td', {}, _('密钥版本')), E('td', {}, key.cached ? String(key.key_version) : '-') ]),
			E('tr', {}, [ E('td', {}, _('RSA 位数')), E('td', {}, key.cached ? String(key.rsa_bits) : '-') ]),
			E('tr', {}, [ E('td', {}, _('SHA-256')), E('td', { 'class': 'bba-code' }, fingerprint) ])
		]))
	]);
}

function renderDevices() {
	const config = snapshot.config || {};
	const rows = devices.map(function(device) {
		const selected = config.scope === 'device' && config.target_ip === device.ip && (!config.target_mac || config.target_mac === device.mac);
		return E('tr', {}, [
			E('td', {}, [ E('strong', {}, device.name || _('未命名设备')), E('div', { 'class': 'bba-muted' }, device.active ? _('当前在线') : _('历史设备')) ]),
			E('td', { 'class': 'bba-code' }, device.ip),
			E('td', { 'class': 'bba-code' }, device.mac || '-'),
			E('td', {}, selected ? badge(_('正在使用'), 'ok') : button(_('用于加速'), 'cbi-button-neutral', function() { showDeviceModal(device); }))
		]);
	});
	return E('div', {}, [
		config.scope === 'lan'
			? E('div', { 'class': 'bba-callout bba-callout-info' }, _('当前作用于整个局域网，无需逐台选择设备。'))
			: null,
		section(_('指定设备'), config.target_ip ? [
			button(_('清除选择'), 'cbi-button-negative', function(ev) {
				return withBusy(ev, function() { return saveConfig({ target_name: '', target_ip: '', target_mac: '' }); });
			})
		] : [], config.target_ip ? table([ _('名称'), 'IPv4', 'MAC' ], [
			E('tr', {}, [ E('td', {}, config.target_name || _('未命名设备')), E('td', { 'class': 'bba-code' }, config.target_ip), E('td', { 'class': 'bba-code' }, config.target_mac || '-') ])
		]) : E('div', { 'class': 'bba-empty' }, _('尚未选择加速设备'))),
		section(_('LAN 设备'), [
			button(_('刷新'), 'cbi-button-neutral', function(ev) {
				return withBusy(ev, function() { return getDevices().then(refreshView); });
			}),
			button(_('手动添加'), 'cbi-button-add', function() { showDeviceModal(null); })
		], rows.length ? table([ _('设备'), 'IPv4', 'MAC', _('操作') ], rows) : E('div', { 'class': 'bba-empty' }, _('没有发现 DHCP 设备')))
	]);
}

function positiveId(value) {
	return /^[1-9][0-9]*$/.test(String(value || ''));
}

function controlButton(label, operation, disabled) {
	return button(label, 'cbi-button-neutral', function(ev) {
		return withBusy(ev, function() {
			return runRequest({ operation: operation });
		});
	}, disabled);
}

function renderAcceleration() {
	const config = snapshot.config || {};
	const session = snapshot.session || {};
	const key = snapshot.acceleration_key || {};
	const dataPlane = snapshot.data_plane || {};
	const traffic = snapshot.traffic || {};
	const profiles = profileMap();
	const selectedIds = Array.isArray(config.selected_games) ? config.selected_games : [];
	const selectedRows = selectedIds.filter(function(id) { return profiles[id]; }).map(function(id) {
		const profile = profiles[id];
		const mode = profile.match_mode === 'provider-profile'
			? _('服务端规则')
			: _('服务端 + 本地特征');
		return E('tr', {}, [
			E('td', {}, E('strong', {}, profile.name || id)),
			E('td', {}, profile.category === 'game' ? _('游戏') : _('平台')),
			E('td', {}, mode),
			E('td', {}, badge(_('已选择'), 'ok'))
		]);
	});
	const scopeControl = E('div', { 'class': 'bba-segmented' }, [
		button(_('整个局域网'), config.scope === 'lan' ? 'cbi-button-positive' : 'cbi-button-neutral', function(ev) {
			return withBusy(ev, function() { return saveConfig({ scope: 'lan' }); });
		}),
		button(_('指定设备'), config.scope === 'device' ? 'cbi-button-positive' : 'cbi-button-neutral', function(ev) {
			return withBusy(ev, function() { return saveConfig({ scope: 'device' }); });
		})
	]);
	const hasLocalHint = selectedIds.some(function(id) {
		return id === 'steam' || id === 'counter-strike-2';
	});
	const idsReady = positiveId(config.target_id) && positiveId(config.area_id) &&
		positiveId(config.platform_id);
	const profileReady = !!dataPlane.profile_cached;
	const authorizationReady = !!dataPlane.authorization_cached;
	const runtimeReady = !!dataPlane.runtime_cached;
	const calloutClass = snapshot.accelerating || snapshot.phase === 'ready'
		? 'bba-callout bba-callout-info' : 'bba-callout';
	const actionButton = snapshot.accelerating
		? button(_('停止加速'), 'cbi-button-negative', function(ev) {
			return withBusy(ev, function() {
				return runRequest({ operation: 'acceleration_action', action: 'stop' });
			});
		})
		: button(_('开始加速'), 'cbi-button-positive', function(ev) {
			return withBusy(ev, function() {
				return runRequest({ operation: 'acceleration_action', action: 'start' });
			});
		}, !snapshot.data_plane_ready || !hasLocalHint);
	return E('div', {}, [
		E('div', { 'class': calloutClass }, snapshot.phase_message || _('等待配置')),
		section(_('加速范围'), [], E('div', { 'class': 'bba-scope-row' }, [
			scopeControl,
			E('span', { 'class': 'bba-muted' }, config.scope === 'device'
				? (config.target_name || config.target_ip || _('尚未选择设备'))
				: _('整个局域网'))
		])),
		section(_('内置游戏'), [ button(_('选择游戏'), 'cbi-button-add', showGameModal) ], selectedRows.length
			? table([ _('名称'), _('类型'), _('匹配来源'), _('状态') ], selectedRows)
			: E('div', { 'class': 'bba-empty' }, _('尚未选择游戏'))),
		renderMatchStatus(),
		section(_('高级参数'), [ button(_('编辑'), 'cbi-button-neutral', showProfileModal) ], table([ _('项目'), _('当前值') ], [
			E('tr', {}, [ E('td', {}, _('游戏 ID')), E('td', { 'class': 'bba-code' }, config.target_id || '-') ]),
			E('tr', {}, [ E('td', {}, _('区服 ID')), E('td', { 'class': 'bba-code' }, config.area_id || '-') ]),
			E('tr', {}, [ E('td', {}, _('平台 ID')), E('td', { 'class': 'bba-code' }, config.platform_id || '-') ]),
			E('tr', {}, [ E('td', {}, _('日志级别')), E('td', {}, config.log_level || 'info') ]),
			E('tr', {}, [ E('td', {}, _('OpenClash 冲突策略')), E('td', {}, config.openclash_mode || 'exclusive') ])
		])),
		section(_('服务端控制'), [
			controlButton(_('刷新游戏目录'), 'game_list', !session.authenticated || !key.cached),
			controlButton(_('检查权益'), 'check_speedup', !session.authenticated || !key.cached || !idsReady),
			controlButton(_('获取节点配置'), 'profile_fetch', !session.authenticated || !key.cached || !idsReady),
			controlButton(_('登录数据通道'), 'signal_login', !session.authenticated || !key.cached || !profileReady || !idsReady),
			controlButton(_('续期数据通道'), 'channel_renew', !session.authenticated || !key.cached || !profileReady || !authorizationReady || !idsReady),
			controlButton(_('生成运行时配置'), 'runtime_prepare', !profileReady || !authorizationReady)
		], E('div', { 'class': 'bba-callout bba-callout-info' }, _('控制 API 会把授权后的节点和通道凭据保存在路由器 root 私有目录；不会在页面或日志中显示令牌。'))),
		section(_('数据通道'), [
			actionButton
		], table([ _('状态'), _('结果') ], [
			E('tr', {}, [ E('td', {}, _('控制 API')), E('td', {}, snapshot.capabilities && snapshot.capabilities.control_api ? badge(_('可用'), 'ok') : badge(_('未安装'), 'warn')) ]),
			E('tr', {}, [ E('td', {}, _('节点配置')), E('td', {}, profileReady ? badge(_('已缓存'), 'ok') : badge(_('未获取'), 'idle')) ]),
			E('tr', {}, [ E('td', {}, _('数据通道授权')), E('td', {}, authorizationReady ? badge(_('已授权'), 'ok') : badge(_('未授权'), 'idle')) ]),
			E('tr', {}, [ E('td', {}, _('运行时配置')), E('td', {}, runtimeReady ? badge(_('已生成'), 'ok') : badge(_('未生成'), 'idle')) ]),
			E('tr', {}, [ E('td', {}, _('Bolt TCP/UDP')), E('td', {}, snapshot.capabilities && snapshot.capabilities.data_channel ? badge(_('可用'), 'ok') : badge(_('未安装'), 'warn')) ]),
			E('tr', {}, [ E('td', {}, _('游戏流量匹配')), E('td', {}, selectedRows.length ? badge(_('规则已选择'), 'info') : badge(_('未配置'), 'idle')) ]),
			E('tr', {}, [ E('td', {}, _('流量路由')), E('td', {}, traffic.accelerating ? badge(_('已接管'), 'ok') : badge(_('未启用'), 'idle')) ])
		]))
	]);
}

function renderMatchStatus() {
	const status = matchStatus || {};
	const flows = Array.isArray(status.flows) ? status.flows : [];
	const rows = flows.map(function(flow) {
		return E('tr', {}, [
			E('td', {}, String(flow.protocol || '').toUpperCase()),
			E('td', { 'class': 'bba-code' }, flow.source_ip || '-'),
			E('td', { 'class': 'bba-code' }, flow.destination_ip || '-'),
			E('td', { 'class': 'bba-code' }, formatNumber(flow.destination_port)),
			E('td', {}, formatNumber(flow.packets)),
			E('td', {}, formatBytes(flow.bytes))
		]);
	});
	let content;
	if (!status.available) {
		content = E('div', { 'class': 'bba-callout' }, status.error || _('实时匹配不可用'));
	} else if (!status.hints_available) {
		content = E('div', { 'class': 'bba-callout bba-callout-info' }, _('当前所选目录没有可安全用于本地端口识别的提示；请先获取服务端 profile 再启动。'));
	} else if (!rows.length) {
		content = E('div', { 'class': 'bba-empty' }, _('暂未发现命中的游戏流量。打开游戏并进入联网/匹配页面后刷新。'));
	} else {
		content = table([ _('协议'), _('来源设备'), _('目标地址'), _('目标端口'), _('包数'), _('流量') ], rows);
	}
	if (status.sample_truncated)
		content = E('div', {}, [ content, E('div', { 'class': 'bba-callout bba-callout-info' }, _('命中连接较多，仅显示前 64 条；顶部计数仍按全部连接统计。')) ]);
	return section(_('实时匹配'), [
		button(_('刷新'), 'cbi-button-neutral', function(ev) {
			return withBusy(ev, function() {
				return getMatchStatus().then(function(next) {
					matchStatus = next;
					if (appNode && activeTab === 'acceleration')
						dom.content(appNode, renderPage());
				});
			});
		})
	], E('div', {}, [
		E('div', { 'class': 'bba-stat-grid' }, [
			E('div', { 'class': 'bba-stat' }, [ E('span', { 'class': 'bba-muted' }, _('命中连接')), E('strong', {}, status.available ? formatNumber(status.flow_count) : '-') ]),
			E('div', { 'class': 'bba-stat' }, [ E('span', { 'class': 'bba-muted' }, _('包数')), E('strong', {}, status.available ? formatNumber(status.packets) : '-') ]),
			E('div', { 'class': 'bba-stat' }, [ E('span', { 'class': 'bba-muted' }, _('流量')), E('strong', {}, status.available ? formatBytes(status.bytes) : '-') ]),
			E('div', { 'class': 'bba-stat' }, [ E('span', { 'class': 'bba-muted' }, _('接管')), E('strong', {}, status.traffic_steering ? _('已启用') : _('未启用')) ])
		]),
		content
	]));
}

function loadLogs() {
	return fs.exec(COMMAND, [ 'logs' ]).then(function(res) {
		if (!res || res.code !== 0)
			throw commandError(res, _('读取日志失败'));
		logText = res.stdout || '';
		if (appNode)
			dom.content(appNode, renderPage());
	});
}

function renderLogs() {
	return section(_('管理日志'), [
		button(_('读取'), 'cbi-button-neutral', function(ev) { return withBusy(ev, loadLogs); }),
		button(_('清空'), 'cbi-button-negative', function(ev) {
			return withBusy(ev, function() {
				return runRequest({ operation: 'clear_logs' }, { refresh: false }).then(function() {
					logText = '';
					if (appNode)
						dom.content(appNode, renderPage());
				});
			});
		})
	], E('textarea', { 'class': 'bba-log', readonly: 'readonly', wrap: 'off' }, logText));
}

function renderTab() {
	switch (activeTab) {
	case 'account': return renderAccount();
	case 'devices': return renderDevices();
	case 'acceleration': return renderAcceleration();
	case 'logs': return renderLogs();
	default: return renderOverview();
	}
}

function renderPage() {
	const serviceButtons = snapshot.manager_running ? [
		button(_('重启管理服务'), 'cbi-button-action', function(ev) {
			return withBusy(ev, function() { return serviceAction('restart'); });
		}),
		button(_('停止'), 'cbi-button-negative', function(ev) {
			return withBusy(ev, function() { return serviceAction('stop'); });
		})
	] : [ button(_('启动管理服务'), 'cbi-button-positive', function(ev) {
		return withBusy(ev, function() { return serviceAction('start'); });
	}) ];
	return E('div', { 'class': 'bba-page' }, [
		E('link', { rel: 'stylesheet', href: L.resource('view/biubiu-acc/main.css') }),
		E('div', { 'class': 'bba-head' }, [
			E('div', {}, [
				E('h2', {}, 'biubiu 加速器'),
				E('div', { 'class': 'bba-statusline' }, [ phaseBadge(), E('span', { 'class': 'bba-version' }, 'v' + (snapshot.version || 'unknown')) ])
			]),
			E('div', { 'class': 'bba-actions' }, serviceButtons)
		]),
		E('nav', { 'class': 'bba-tabs' }, TABS.map(function(tab) {
			return E('button', {
				type: 'button',
				'class': 'bba-tab' + (activeTab === tab[0] ? ' active' : ''),
				click: function() {
					activeTab = tab[0];
					if (appNode)
						dom.content(appNode, renderPage());
				}
			}, tab[1]);
		})),
		renderTab()
	]);
}

return view.extend({
	handleSaveApply: null,
	handleSave: null,
	handleReset: null,

	load: function() {
		return Promise.all([
			getSnapshot(),
			getDevices(),
			L.resolveDefault(getCatalog(), { profiles: [] }),
			L.resolveDefault(getMatchStatus(), { available: false, flows: [] })
		]).then(function(data) {
			snapshot = data[0];
			catalog = data[2];
			matchStatus = data[3];
			return data;
		});
	},

	render: function() {
		appNode = E('div', {}, renderPage());
		poll.add(function() {
			const matchRequest = activeTab === 'acceleration'
				? L.resolveDefault(getMatchStatus(), matchStatus)
				: Promise.resolve(matchStatus);
			return Promise.all([ getSnapshot(), matchRequest ]).then(function(data) {
				snapshot = data[0];
				matchStatus = data[1];
				if (appNode && !document.querySelector('.modal'))
					dom.content(appNode, renderPage());
			}).catch(function() {});
		}, 5);
		return appNode;
	}
});
