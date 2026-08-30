'use strict';
'require dom';
'require fs';
'require network';
'require poll';
'require rpc';
'require ui';
'require view';

const COMMAND = '/usr/libexec/cloudflare-tunnel-manager';
const REQUEST_FILE = '/tmp/cloudflare-tunnel-manager/request.json';
const TABS = [
	[ 'overview', _('总览') ],
	[ 'accounts', _('CF 账号') ],
	[ 'tunnels', _('Tunnel') ],
	[ 'routes', _('穿透规则') ],
	[ 'logs', _('日志') ]
];

const callDHCPLeases = rpc.declare({
	object: 'luci-rpc',
	method: 'getDHCPLeases',
	expect: { '': {} }
});

let appNode = null;
let activeTab = 'overview';
let snapshot = { accounts: [], tunnels: [], routes: [] };
let devices = [];
let metricSamples = Object.create(null);
let logTarget = 'manager';
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
		return parseJSONCommand(res, _('无法读取 Cloudflare Tunnel 状态'));
	});
}

function applyMetrics(next) {
	const nextSamples = Object.create(null);
	(next.tunnels || []).forEach(function(tunnel) {
		const previous = metricSamples[tunnel.section];
		const processTicks = Number(tunnel.process_ticks) || 0;
		const systemTicks = Number(next.system_ticks) || 0;
		const cpuCount = Math.max(1, Number(next.cpu_count) || 1);
		const uptime = Math.max(0, Number(tunnel.uptime_seconds) || 0);
		let cpu = uptime > 0 ? processTicks / uptime : 0;
		if (previous && previous.pid === tunnel.pid && processTicks >= previous.processTicks &&
			systemTicks > previous.systemTicks) {
			cpu = (processTicks - previous.processTicks) /
				(systemTicks - previous.systemTicks) * cpuCount * 100;
		}
		tunnel._cpu = Math.max(0, Math.min(cpuCount * 100, cpu));
		tunnel._memory = Number(next.memory_total_kb) > 0
			? (Number(tunnel.rss_kb) || 0) / Number(next.memory_total_kb) * 100
			: 0;
		nextSamples[tunnel.section] = {
			pid: tunnel.pid,
			processTicks: processTicks,
			systemTicks: systemTicks
		};
	});
	metricSamples = nextSamples;
	snapshot = next;
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
			return L.naturalCompare(a.name || a.ip, b.name || b.ip);
		});
		return devices;
	});
}

function readRemote(kind, account) {
	return fs.exec(COMMAND, [ kind, account ]).then(function(res) {
		const payload = parseJSONCommand(res, _('Cloudflare API 查询失败'));
		if (payload.success === false)
			throw new Error(payload.errors && payload.errors[0] && payload.errors[0].message || _('Cloudflare API 查询失败'));
		return payload.result || [];
	});
}

function notifyError(err) {
	ui.addNotification(null, E('p', {}, err && err.message || String(err)), 'error');
}

function refreshView() {
	return getSnapshot().then(function(next) {
		applyMetrics(next);
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

function button(label, style, handler, disabled) {
	return E('button', {
		type: 'button',
		'class': 'cbi-button ' + (style || 'cbi-button-neutral'),
		disabled: disabled ? 'disabled' : null,
		click: handler
	}, label);
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

function check(label, checked) {
	const node = input('checkbox');
	node.checked = !!checked;
	return { input: node, row: E('label', { 'class': 'ctm-check' }, [ node, label ]) };
}

function fieldRows(rows) {
	const children = [];
	rows.forEach(function(row) {
		children.push(E('label', {}, row[0]), row[1]);
	});
	return E('div', { 'class': 'ctm-form' }, children);
}

function modalActions(actions) {
	return E('div', { 'class': 'right ctm-actions' }, actions);
}

function closeModal() {
	ui.hideModal();
}

function accountBySection(section) {
	return (snapshot.accounts || []).find(function(account) { return account.section === section; });
}

function tunnelBySection(section) {
	return (snapshot.tunnels || []).find(function(tunnel) { return tunnel.section === section; });
}

function formatKiB(value) {
	value = Number(value) || 0;
	if (value >= 1024 * 1024)
		return (value / (1024 * 1024)).toFixed(1) + ' GB';
	return (value / 1024).toFixed(1) + ' MB';
}

function formatDuration(value) {
	let seconds = Math.max(0, Math.floor(Number(value) || 0));
	const days = Math.floor(seconds / 86400);
	const hours = Math.floor(seconds % 86400 / 3600);
	const minutes = Math.floor(seconds % 3600 / 60);
	if (days)
		return '%d 天 %d 小时'.format(days, hours);
	if (hours)
		return '%d 小时 %d 分'.format(hours, minutes);
	if (minutes)
		return '%d 分 %d 秒'.format(minutes, seconds % 60);
	return '%d 秒'.format(seconds);
}

function formatDate(epoch) {
	const value = Number(epoch) || 0;
	return value ? new Date(value * 1000).toLocaleString() : _('未验证');
}

function badge(label, kind) {
	return E('span', { 'class': 'ctm-badge ctm-' + kind }, label);
}

function progress(value) {
	const width = Math.max(0, Math.min(100, Number(value) || 0));
	return E('div', { 'class': 'ctm-progress' }, E('i', { style: 'width:' + width.toFixed(1) + '%' }));
}

function empty(message) {
	return E('div', { 'class': 'ctm-empty' }, message);
}

function table(headers, rows) {
	return E('div', { 'class': 'ctm-table-wrap' }, E('table', { 'class': 'ctm-table' }, [
		E('tr', {}, headers.map(function(header) { return E('th', {}, header); }))
	].concat(rows)));
}

function section(title, actions, content) {
	return E('section', { 'class': 'ctm-section' }, [
		E('div', { 'class': 'ctm-section-head' }, [ E('h3', {}, title), E('div', { 'class': 'ctm-actions' }, actions || []) ]),
		content
	]);
}

function showConfirm(title, message, confirmLabel, handler, danger) {
	ui.showModal(title, [
		E('p', {}, message),
		modalActions([
			button(_('取消'), 'cbi-button-neutral', closeModal),
			button(confirmLabel, danger ? 'cbi-button-negative' : 'cbi-button-action', function(ev) {
				return withBusy(ev, function() {
					return handler().then(closeModal);
				});
			})
		])
	]);
}

function showAccountModal(account) {
	const name = input('text', account && account.name, _('例如：主账号'));
	const accountId = input('text', account && account.account_id, '32 位 Account ID');
	const token = input('password', '', account ? _('留空则不更换') : _('粘贴 API Token'));
	ui.showModal(account ? _('编辑 CF 账号') : _('添加 CF 账号'), [
		fieldRows([
			[ _('账号名称'), name ],
			[ _('Account ID'), accountId ],
			[ _('API Token'), token ]
		]),
		E('div', { 'class': 'ctm-callout', style: 'margin-top:14px' },
			_('Token 必须包含 Account / Cloudflare Tunnel / Edit、Zone / Zone / Read、Zone / DNS / Edit。Token 只保存在路由器的 0600 密钥文件中。')),
		modalActions([
			button(_('取消'), 'cbi-button-neutral', closeModal),
			button(_('验证并保存'), 'cbi-button-positive', function(ev) {
				return withBusy(ev, function() {
					return runRequest({
						operation: 'save_account',
						section: account && account.section || '',
						name: name.value.trim(),
						account_id: accountId.value.trim(),
						token: token.value.trim()
					}).then(function(result) {
						token.value = '';
						closeModal();
						return result;
					});
				});
			})
		])
	]);
}

function showSettingsModal() {
	const enabled = check(_('开机启动并运行实例'), snapshot.manager_enabled);
	const iface = input('text', snapshot.interface || 'wan', 'wan');
	const level = select([
		[ 'debug', 'Debug' ], [ 'info', 'Info' ], [ 'warn', 'Warn' ],
		[ 'error', 'Error' ], [ 'fatal', 'Fatal' ]
	], snapshot.loglevel || 'info');
	ui.showModal(_('全局设置'), [
		fieldRows([
			[ _('服务状态'), enabled.row ],
			[ _('联网接口'), iface ],
			[ _('日志级别'), level ]
		]),
		modalActions([
			button(_('取消'), 'cbi-button-neutral', closeModal),
			button(_('保存'), 'cbi-button-positive', function(ev) {
				return withBusy(ev, function() {
					return runRequest({
						operation: 'save_settings',
						enabled: enabled.input.checked,
						interface: iface.value.trim(),
						loglevel: level.value
					}).then(closeModal);
				});
			})
		])
	]);
}

function showCreateTunnelModal() {
	if (!(snapshot.accounts || []).length) {
		showAccountModal(null);
		return;
	}
	const account = select(snapshot.accounts.map(function(item) {
		return [ item.section, item.name ];
	}));
	const name = input('text', '', _('例如：home-router'));
	ui.showModal(_('新建 Tunnel'), [
		fieldRows([
			[ _('CF 账号'), account ],
			[ _('Tunnel 名称'), name ]
		]),
		modalActions([
			button(_('取消'), 'cbi-button-neutral', closeModal),
			button(_('创建'), 'cbi-button-positive', function(ev) {
				return withBusy(ev, function() {
					return runRequest({ operation: 'create_tunnel', account: account.value, name: name.value.trim() })
						.then(closeModal);
				});
			})
		])
	]);
}

function showImportTunnelModal() {
	if (!(snapshot.accounts || []).length) {
		showAccountModal(null);
		return;
	}
	const account = select(snapshot.accounts.map(function(item) { return [ item.section, item.name ]; }));
	const remote = select([ [ '', _('先读取远端 Tunnel') ] ]);
	remote.disabled = true;
	const enabled = check(_('导入后立即启动'), false);
	function loadRemote(ev) {
		return withBusy(ev, function() {
			remote.disabled = true;
			return readRemote('remote-tunnels', account.value).then(function(items) {
				const localIds = (snapshot.tunnels || []).map(function(t) { return t.tunnel_id; });
				dom.content(remote, items.length ? items.map(function(item) {
					const imported = localIds.indexOf(item.id) !== -1;
					return E('option', { value: item.id, disabled: imported ? 'disabled' : null },
						item.name + (imported ? _('（已导入）') : ''));
				}) : E('option', { value: '' }, _('没有可导入的 Tunnel')));
				remote.disabled = !items.length;
			});
		});
	}
	const load = button(_('读取远端'), 'cbi-button-neutral', loadRemote);
	ui.showModal(_('导入现有 Tunnel'), [
		fieldRows([
			[ _('CF 账号'), E('div', { 'class': 'ctm-inline' }, [ account, load ]) ],
			[ _('远端 Tunnel'), remote ],
			[ _('运行状态'), enabled.row ]
		]),
		E('div', { 'class': 'ctm-callout', style: 'margin-top:14px' },
			_('导入项只负责运行现有 Tunnel，不改写其远端 Ingress 和 DNS。')),
		modalActions([
			button(_('取消'), 'cbi-button-neutral', closeModal),
			button(_('导入'), 'cbi-button-positive', function(ev) {
				return withBusy(ev, function() {
					if (!remote.value)
						throw new Error(_('请选择远端 Tunnel'));
					return runRequest({
						operation: 'import_tunnel', account: account.value,
						tunnel_id: remote.value, enabled: enabled.input.checked
					}).then(closeModal);
				});
			})
		])
	]);
}

function showTunnelSettingsModal(tunnel) {
	const enabled = check(_('运行这个实例'), tunnel.enabled);
	const protocol = select([ [ 'auto', 'Auto' ], [ 'quic', 'QUIC' ], [ 'http2', 'HTTP/2' ] ], tunnel.protocol || 'auto');
	const ipVersion = select([ [ 'auto', 'Auto' ], [ '4', 'IPv4' ], [ '6', 'IPv6' ] ], tunnel.edge_ip_version || 'auto');
	const bind = input('text', tunnel.edge_bind_address || '', _('可选'));
	const grace = input('text', tunnel.grace_period || '', '30s');
	const retries = input('number', tunnel.retries || '', _('可选'));
	const region = input('text', tunnel.region || '', _('可选'));
	const metrics = input('text', tunnel.metrics || '', '127.0.0.1:2000');
	ui.showModal(_('Tunnel 运行参数'), [
		fieldRows([
			[ _('运行状态'), enabled.row ],
			[ _('连接协议'), protocol ],
			[ _('边缘 IP'), ipVersion ],
			[ _('绑定地址'), bind ],
			[ _('优雅退出'), grace ],
			[ _('重试次数'), retries ],
			[ _('Region'), region ],
			[ _('Metrics'), metrics ]
		]),
		modalActions([
			button(_('取消'), 'cbi-button-neutral', closeModal),
			button(_('保存并重载'), 'cbi-button-positive', function(ev) {
				return withBusy(ev, function() {
					return runRequest({
						operation: 'save_tunnel_settings', section: tunnel.section,
						enabled: enabled.input.checked, protocol: protocol.value,
						edge_ip_version: ipVersion.value, edge_bind_address: bind.value.trim(),
						grace_period: grace.value.trim(), retries: retries.value.trim(),
						region: region.value.trim(), metrics: metrics.value.trim()
					}).then(closeModal);
				});
			})
		])
	]);
}

function showRouteModal(route) {
	const managedTunnels = (snapshot.tunnels || []).filter(function(tunnel) { return tunnel.managed; });
	if (!managedTunnels.length) {
		showCreateTunnelModal();
		return;
	}
	const currentTunnel = route && route.tunnel || managedTunnels[0].section;
	const tunnel = select(managedTunnels.map(function(item) {
		const account = accountBySection(item.account);
		return [ item.section, (account ? account.name + ' / ' : '') + item.name ];
	}), currentTunnel);
	if (route)
		tunnel.disabled = true;
	const zone = select([ [ '', _('正在读取域名...') ] ], route && route.zone_id);
	zone.disabled = true;
	const hostname = input('text', route && route.hostname, 'service.example.com');
	const device = select([ [ '', _('自定义 IP') ] ].concat(devices.map(function(item) {
		return [ item.mac || item.ip, (item.name || _('未知设备')) + ' · ' + item.ip ];
	})), route && (route.device_mac || route.origin_ip));
	const deviceName = input('text', route && route.device_name, _('可选'));
	const deviceMac = input('text', route && route.device_mac, _('可选'));
	const originIp = input('text', route && route.origin_ip, '192.168.100.10');
	const protocol = select([
		[ 'http', 'HTTP' ], [ 'https', 'HTTPS' ], [ 'ssh', 'SSH' ],
		[ 'tcp', 'TCP' ], [ 'rdp', 'RDP' ], [ 'smb', 'SMB' ]
	], route && route.protocol || 'http');
	const port = input('number', route && route.port || '80', '80');
	const enabled = check(_('同步 Ingress 与 DNS'), route ? route.enabled : true);
	const reserve = check(_('保存为 DHCP 静态租约'), false);
	const path = input('text', route && route.path, _('可选，例如 /api/*'));
	const noTls = check(_('跳过源站 TLS 证书验证'), route && route.no_tls_verify);
	const serverName = input('text', route && route.origin_server_name, _('可选'));
	const hostHeader = input('text', route && route.http_host_header, _('可选'));
	const timeout = input('number', route && route.connect_timeout || '', '30');

	function setDevice() {
		const selected = devices.find(function(item) { return (item.mac || item.ip) === device.value; });
		if (!selected)
			return;
		deviceName.value = selected.name || '';
		deviceMac.value = selected.mac || '';
		originIp.value = selected.ip || '';
		reserve.input.disabled = !selected.mac;
	}
	device.addEventListener('change', setDevice);

	function setDefaultPort() {
		const defaults = { http: 80, https: 443, ssh: 22, rdp: 3389, smb: 445 };
		if (defaults[protocol.value])
			port.value = defaults[protocol.value];
	}
	protocol.addEventListener('change', setDefaultPort);

	function loadZones() {
		const selectedTunnel = tunnelBySection(tunnel.value);
		if (!selectedTunnel)
			return Promise.reject(new Error(_('Tunnel 配置不存在')));
		zone.disabled = true;
		dom.content(zone, E('option', { value: '' }, _('正在读取域名...')));
		return readRemote('zones', selectedTunnel.account).then(function(items) {
			dom.content(zone, items.length ? items.map(function(item) {
				return E('option', {
					value: item.id,
					selected: route && route.zone_id === item.id ? 'selected' : null
				}, item.name);
			}) : E('option', { value: '' }, _('账号下没有可用 Zone')));
			zone.disabled = !items.length;
		});
	}
	tunnel.addEventListener('change', function() { loadZones().catch(notifyError); });

	const baseForm = fieldRows([
		[ _('Tunnel'), tunnel ],
		[ _('Cloudflare Zone'), zone ],
		[ _('完整域名'), hostname ],
		[ _('LAN 设备'), device ],
		[ _('设备名称'), deviceName ],
		[ _('设备 MAC'), deviceMac ],
		[ _('源站 IPv4'), originIp ],
		[ _('源站协议'), protocol ],
		[ _('源站端口'), port ],
		[ _('规则状态'), enabled.row ],
		[ _('地址稳定'), reserve.row ]
	]);
	const advanced = E('details', { 'class': 'ctm-advanced', style: 'margin-top:14px' }, [
		E('summary', {}, _('高级源站参数')),
		fieldRows([
			[ _('URL 路径'), path ],
			[ _('TLS 校验'), noTls.row ],
			[ _('Origin Server Name'), serverName ],
			[ _('HTTP Host Header'), hostHeader ],
			[ _('连接超时（秒）'), timeout ]
		])
	]);

	ui.showModal(route ? _('编辑穿透规则') : _('新建穿透规则'), [
		baseForm,
		advanced,
		modalActions([
			button(_('取消'), 'cbi-button-neutral', closeModal),
			button(_('测试源站'), 'cbi-button-neutral', function(ev) {
				return withBusy(ev, function() {
					return runRequest({ operation: 'test_origin', protocol: protocol.value,
						origin_ip: originIp.value.trim(), port: port.value }, { refresh: false });
				});
			}),
			button(_('保存并同步'), 'cbi-button-positive', function(ev) {
				return withBusy(ev, function() {
					let chain = Promise.resolve();
					if (reserve.input.checked) {
						chain = runRequest({ operation: 'reserve_device', name: deviceName.value.trim(),
							mac: deviceMac.value.trim(), ip: originIp.value.trim() }, { refresh: false });
					}
					return chain.then(function() {
						return runRequest({
							operation: 'save_route', section: route && route.section || '',
							tunnel: tunnel.value, zone_id: zone.value,
							hostname: hostname.value.trim(), device_name: deviceName.value.trim(),
							device_mac: deviceMac.value.trim(), origin_ip: originIp.value.trim(),
							protocol: protocol.value, port: port.value, enabled: enabled.input.checked,
							path: path.value.trim(), no_tls_verify: noTls.input.checked,
							origin_server_name: serverName.value.trim(),
							http_host_header: hostHeader.value.trim(), connect_timeout: timeout.value
						});
					}).then(closeModal);
				});
			})
		])
	]);
	loadZones().catch(notifyError);
	if (route && route.device_mac)
		device.value = route.device_mac;
}

function renderOverview() {
	const tunnels = snapshot.tunnels || [];
	const running = tunnels.filter(function(tunnel) { return tunnel.running; });
	const memory = running.reduce(function(total, tunnel) { return total + (Number(tunnel.rss_kb) || 0); }, 0);
	const rows = tunnels.map(function(tunnel) {
		const account = accountBySection(tunnel.account);
		return E('tr', {}, [
			E('td', {}, [ E('strong', {}, tunnel.name), E('div', { 'class': 'ctm-muted' }, account && account.name || '') ]),
			E('td', {}, tunnel.running ? badge(_('运行中'), 'ok') : badge(tunnel.enabled ? _('等待连接') : _('已停止'), tunnel.enabled ? 'warn' : 'idle')),
			E('td', {}, tunnel.running ? [
				E('span', {}, tunnel._cpu.toFixed(1) + '%'), progress(tunnel._cpu)
			] : '-'),
			E('td', {}, tunnel.running ? [
				E('span', {}, formatKiB(tunnel.rss_kb) + ' · ' + tunnel._memory.toFixed(1) + '%'), progress(tunnel._memory)
			] : '-'),
			E('td', {}, tunnel.running ? formatDuration(tunnel.uptime_seconds) : '-'),
			E('td', { 'class': 'ctm-code' }, tunnel.pid || '-')
		]);
	});
	return E('div', {}, [
		E('div', { 'class': 'ctm-stat-grid' }, [
			E('div', { 'class': 'ctm-stat' }, [ E('span', { 'class': 'ctm-muted' }, _('CF 账号')), E('strong', {}, snapshot.account_count || 0) ]),
			E('div', { 'class': 'ctm-stat' }, [ E('span', { 'class': 'ctm-muted' }, _('运行实例')), E('strong', { 'class': running.length ? 'ctm-ok' : '' }, running.length + ' / ' + tunnels.length) ]),
			E('div', { 'class': 'ctm-stat' }, [ E('span', { 'class': 'ctm-muted' }, _('穿透规则')), E('strong', {}, snapshot.route_count || 0) ]),
			E('div', { 'class': 'ctm-stat' }, [ E('span', { 'class': 'ctm-muted' }, _('实例内存')), E('strong', {}, formatKiB(memory)) ])
		]),
		section(_('实例资源占用'), [ button(_('刷新'), 'cbi-button-neutral', function(ev) { return withBusy(ev, refreshView); }) ],
			rows.length ? table([ _('Tunnel'), _('状态'), 'CPU', _('内存'), _('运行时间'), 'PID' ], rows) : empty(_('尚未创建或导入 Tunnel')))
	]);
}

function renderAccounts() {
	const rows = (snapshot.accounts || []).map(function(account) {
		return E('tr', {}, [
			E('td', {}, E('strong', {}, account.name)),
			E('td', { 'class': 'ctm-code' }, account.account_id),
			E('td', {}, account.token_set ? '•••• ' + account.token_hint : badge(_('未配置'), 'error')),
			E('td', {}, formatDate(account.last_verified)),
			E('td', {}, E('div', { 'class': 'ctm-actions' }, [
				button(_('验证'), 'cbi-button-neutral', function(ev) {
					return withBusy(ev, function() { return runRequest({ operation: 'verify_account', section: account.section }); });
				}),
				button(_('编辑'), 'cbi-button-neutral', function() { showAccountModal(account); }),
				button(_('删除'), 'cbi-button-negative', function() {
					showConfirm(_('删除 CF 账号'), _('只会删除路由器保存的账号和密钥。'), _('删除'),
						function() { return runRequest({ operation: 'delete_account', section: account.section }); }, true);
				})
			]))
		]);
	});
	return section(_('Cloudflare 账号'), [ button(_('添加账号'), 'cbi-button-add', function() { showAccountModal(null); }) ],
		rows.length ? table([ _('名称'), 'Account ID', _('Token'), _('最近验证'), _('操作') ], rows) : empty(_('尚未添加 Cloudflare 账号')));
}

function renderTunnels() {
	const rows = (snapshot.tunnels || []).map(function(tunnel) {
		const account = accountBySection(tunnel.account);
		return E('tr', {}, [
			E('td', {}, [ E('strong', {}, tunnel.name), E('div', { 'class': 'ctm-muted' }, account && account.name || '') ]),
			E('td', {}, tunnel.managed ? badge(_('本机维护'), 'ok') : badge(_('只读导入'), 'warn')),
			E('td', {}, tunnel.running ? badge(_('运行中'), 'ok') : badge(tunnel.enabled ? _('未连接') : _('已停止'), tunnel.enabled ? 'warn' : 'idle')),
			E('td', {}, tunnel.route_count || 0),
			E('td', { 'class': 'ctm-code' }, tunnel.tunnel_id),
			E('td', {}, E('div', { 'class': 'ctm-actions' }, [
				button(tunnel.enabled ? _('停止') : _('启动'), tunnel.enabled ? 'cbi-button-negative' : 'cbi-button-positive', function(ev) {
					return withBusy(ev, function() { return runRequest({ operation: 'toggle_tunnel', section: tunnel.section, enabled: !tunnel.enabled }); });
				}),
				button(_('参数'), 'cbi-button-neutral', function() { showTunnelSettingsModal(tunnel); }),
				button(_('删除'), 'cbi-button-negative', function() {
					const remote = check(_('同时删除 Cloudflare 远端 Tunnel'), false);
					ui.showModal(_('删除 Tunnel'), [
						E('p', {}, _('存在穿透规则时不会执行删除。')),
						remote.row,
						modalActions([
							button(_('取消'), 'cbi-button-neutral', closeModal),
							button(_('删除'), 'cbi-button-negative', function(ev) {
								return withBusy(ev, function() {
									return runRequest({ operation: 'delete_tunnel', section: tunnel.section,
										delete_remote: remote.input.checked }).then(closeModal);
								});
							})
						])
					]);
				})
			]))
		]);
	});
	return section(_('Tunnel 实例'), [
		button(_('导入'), 'cbi-button-neutral', showImportTunnelModal),
		button(_('新建 Tunnel'), 'cbi-button-add', showCreateTunnelModal)
	], rows.length ? table([ _('名称'), _('管理模式'), _('状态'), _('规则'), 'Tunnel ID', _('操作') ], rows) : empty(_('尚未创建或导入 Tunnel')));
}

function renderRoutes() {
	const rows = (snapshot.routes || []).map(function(route) {
		const tunnel = tunnelBySection(route.tunnel);
		return E('tr', {}, [
			E('td', {}, [ E('strong', {}, route.hostname), E('div', { 'class': 'ctm-muted' }, route.zone_name || '') ]),
			E('td', {}, route.device_name || '-'),
			E('td', { 'class': 'ctm-code' }, route.origin_ip + ':' + route.port),
			E('td', {}, String(route.protocol || '').toUpperCase()),
			E('td', {}, tunnel && tunnel.name || '-'),
			E('td', {}, route.enabled ? badge(route.managed_dns ? _('已托管') : _('沿用 DNS'), route.managed_dns ? 'ok' : 'warn') : badge(_('已禁用'), 'idle')),
			E('td', {}, E('div', { 'class': 'ctm-actions' }, [
				button(_('编辑'), 'cbi-button-neutral', function() { showRouteModal(route); }),
				button(_('删除'), 'cbi-button-negative', function() {
					showConfirm(_('删除穿透规则'), _('将同步删除 Ingress；仅删除由本管理器创建的 DNS。'), _('删除并同步'),
						function() { return runRequest({ operation: 'delete_route', section: route.section }); }, true);
				})
			]))
		]);
	});
	return section(_('设备与端口穿透'), [ button(_('新建规则'), 'cbi-button-add', function() { showRouteModal(null); }) ],
		rows.length ? table([ _('域名'), _('设备'), _('源站'), _('协议'), 'Tunnel', 'DNS', _('操作') ], rows) : empty(_('尚未配置穿透规则')));
}

function loadLogs(target) {
	return fs.exec(COMMAND, [ 'logs', target ]).then(function(res) {
		if (!res || res.code !== 0)
			throw commandError(res, _('读取日志失败'));
		logText = res.stdout || '';
		if (appNode)
			dom.content(appNode, renderPage());
	});
}

function renderLogs() {
	const targets = [ [ 'manager', _('管理器操作日志') ] ].concat((snapshot.tunnels || []).map(function(tunnel) {
		return [ tunnel.section, tunnel.name ];
	}));
	const target = select(targets, logTarget);
	target.addEventListener('change', function() { logTarget = target.value; });
	return section(_('运行日志'), [
		target,
		button(_('读取'), 'cbi-button-neutral', function(ev) { return withBusy(ev, function() { return loadLogs(target.value); }); }),
		button(_('清空'), 'cbi-button-negative', function(ev) {
			logTarget = target.value;
			return withBusy(ev, function() {
				return runRequest({ operation: 'clear_log', target: logTarget }, { refresh: false }).then(function() {
					logText = '';
					return refreshView();
				});
			});
		})
	], E('textarea', { 'class': 'ctm-log', readonly: 'readonly', wrap: 'off' }, logText));
}

function renderTab() {
	switch (activeTab) {
	case 'accounts': return renderAccounts();
	case 'tunnels': return renderTunnels();
	case 'routes': return renderRoutes();
	case 'logs': return renderLogs();
	default: return renderOverview();
	}
}

function renderPage() {
	const running = (snapshot.tunnels || []).filter(function(tunnel) { return tunnel.running; }).length;
	return E('div', { 'class': 'ctm-page' }, [
		E('link', { rel: 'stylesheet', href: L.resource('view/cloudflare-tunnel-manager/main.css') }),
		E('div', { 'class': 'ctm-head' }, [
			E('div', {}, [
				E('h2', {}, 'Cloudflare Tunnel'),
				E('div', { 'class': 'ctm-statusline' }, [
					running ? badge(_('%d 个实例运行中').format(running), 'ok') : badge(_('没有运行中的实例'), 'idle'),
					E('span', { 'class': 'ctm-version' }, snapshot.version || '')
				])
			]),
			E('div', { 'class': 'ctm-actions' }, [
				button(_('全局设置'), 'cbi-button-neutral', showSettingsModal),
				button(_('重启实例'), 'cbi-button-action', function(ev) {
					return withBusy(ev, function() { return runRequest({ operation: 'restart_service' }); });
				})
			])
		]),
		E('nav', { 'class': 'ctm-tabs' }, TABS.map(function(tab) {
			return E('button', {
				type: 'button',
				'class': 'ctm-tab' + (activeTab === tab[0] ? ' active' : ''),
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
		return Promise.all([ getSnapshot(), getDevices() ]).then(function(data) {
			applyMetrics(data[0]);
			return data;
		});
	},

	render: function() {
		appNode = E('div', {}, renderPage());
		poll.add(function() {
			return getSnapshot().then(function(next) {
				applyMetrics(next);
				if (appNode && !document.querySelector('.modal'))
					dom.content(appNode, renderPage());
			}).catch(function() {});
		}, 5);
		return appNode;
	}
});
