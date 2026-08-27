'use strict';
'require dom';
'require form';
'require fs';
'require poll';
'require rpc';
'require uci';
'require ui';
'require view';

const callServiceList = rpc.declare({
	object: 'service',
	method: 'list',
	params: [ 'name' ],
	expect: { '': {} }
});

const CHAT_COMMAND = '/usr/libexec/agent-hub-chat';
const METRICS_COMMAND = '/usr/libexec/agent-hub-metrics';
let chatBusy = false;
let chatTimer = null;
let chatMessages = [];
let chatServiceRunning = false;
let previousMetrics = null;

function getProcdStatus() {
	return L.resolveDefault(callServiceList('agent-hub'), {}).then(function(res) {
		const instances = res['agent-hub'] && res['agent-hub'].instances || {};
		let fallback = null;
		for (const name in instances) {
			if (!instances[name].running)
				continue;
			fallback = { running: true, engine: name === 'picoclaw-web' ? 'picoclaw' : name };
			if (name !== 'picoclaw-web')
				return fallback;
		}
		return fallback || { running: false, engine: null };
	});
}

function metricNumber(payload, key) {
	const value = Number(payload[key]);
	return Number.isFinite(value) && value >= 0 ? value : 0;
}

function parseMetrics(res) {
	if (!res || res.code !== 0)
		throw new Error(res && (res.stderr || res.stdout) || _('Unable to read resource usage'));

	const payload = JSON.parse(res.stdout || '{}');
	if (!payload.running) {
		previousMetrics = null;
		return { running: false, engine: null };
	}

	const status = {
		running: true,
		engine: payload.engine || null,
		pid: metricNumber(payload, 'pid'),
		processes: metricNumber(payload, 'processes'),
		processTicks: metricNumber(payload, 'process_ticks'),
		systemTicks: metricNumber(payload, 'system_ticks'),
		cpuCount: Math.max(1, metricNumber(payload, 'cpu_count')),
		rssKiB: metricNumber(payload, 'rss_kb'),
		memoryTotalKiB: metricNumber(payload, 'memory_total_kb'),
		uptimeSeconds: metricNumber(payload, 'uptime_seconds')
	};

	let cpuPercent = status.uptimeSeconds > 0
		? status.processTicks / status.uptimeSeconds
		: 0;
	if (previousMetrics && previousMetrics.pid === status.pid &&
	    status.processTicks >= previousMetrics.processTicks &&
	    status.systemTicks > previousMetrics.systemTicks) {
		cpuPercent = (status.processTicks - previousMetrics.processTicks) /
			(status.systemTicks - previousMetrics.systemTicks) * status.cpuCount * 100;
	}
	status.cpuPercent = Math.max(0, Math.min(status.cpuCount * 100, cpuPercent));
	status.memoryPercent = status.memoryTotalKiB > 0
		? status.rssKiB / status.memoryTotalKiB * 100
		: 0;
	previousMetrics = status;
	return status;
}

function getServiceStatus() {
	return fs.exec(METRICS_COMMAND, []).then(parseMetrics).catch(getProcdStatus);
}

function formatKiB(value) {
	if (value >= 1024 * 1024)
		return '%s GB'.format((value / (1024 * 1024)).toFixed(1));
	return '%s MB'.format((value / 1024).toFixed(1));
}

function formatDuration(value) {
	const seconds = Math.max(0, Math.floor(value));
	const days = Math.floor(seconds / 86400);
	const hours = Math.floor(seconds % 86400 / 3600);
	const minutes = Math.floor(seconds % 3600 / 60);
	if (days > 0)
		return '%dd %dh'.format(days, hours);
	if (hours > 0)
		return '%dh %dm'.format(hours, minutes);
	if (minutes > 0)
		return '%dm %ds'.format(minutes, seconds % 60);
	return '%ds'.format(seconds);
}

function renderStatus(status) {
	const color = status.running ? '#16813d' : '#7a7a7a';
	const label = status.running
		? '%s: %s'.format(_('Running'), status.engine)
		: _('Stopped');
	const items = [ E('span', {
		style: 'color:%s;font-weight:600;white-space:nowrap'.format(color)
	}, label) ];

	if (status.running && status.pid) {
		items.push(
			E('span', { style: 'color:#7a7a7a;white-space:nowrap' }, 'PID %s'.format(status.pid)),
			E('span', { style: 'color:#7a7a7a;white-space:nowrap' },
				'CPU %s%'.format(status.cpuPercent.toFixed(1))),
			E('span', { style: 'color:#7a7a7a;white-space:nowrap' },
				'%s %s (%s%)'.format(_('Memory'), formatKiB(status.rssKiB), status.memoryPercent.toFixed(1))),
			E('span', { style: 'color:#7a7a7a;white-space:nowrap' },
				'%s %s'.format(_('Uptime'), formatDuration(status.uptimeSeconds)))
		);
		if (status.processes > 1)
			items.push(E('span', { style: 'color:#7a7a7a;white-space:nowrap' },
				'%s %s'.format(_('Processes'), status.processes)));
	}

	return E('div', {
		style: 'display:flex;align-items:center;gap:14px;flex-wrap:wrap;min-width:0'
	}, items);
}

function runServiceAction(action) {
	return fs.exec('/etc/init.d/agent-hub', [ action ]).then(function(res) {
		if (res.code !== 0) {
			ui.addNotification(null, E('p', {}, [
				_('Service action failed: %s').format(res.stderr || res.stdout || action)
			]));
		}
	});
}

function commandPayload(res) {
	let payload;
	try {
		payload = JSON.parse(res.stdout || '{}');
	}
	catch (err) {
		throw new Error(res.stderr || res.stdout || _('Invalid response from Agent Hub'));
	}

	if (res.code !== 0 && !payload.output)
		throw new Error(res.stderr || _('Agent Hub command failed'));

	return payload;
}

function chatRows() {
	return chatMessages.map(function(message) {
		const accent = message.role === 'user' ? '#0a8f8f' : '#16813d';
		return E('div', {
			style: 'border-left:3px solid %s;padding:8px 10px;margin:0 0 10px 0;min-width:0'.format(accent)
		}, [
			E('div', {
				style: 'font-size:12px;font-weight:600;margin-bottom:4px;color:#7a7a7a'
			}, message.role === 'user' ? _('You') : _('Agent')),
			E('div', {
				style: 'white-space:pre-wrap;overflow-wrap:anywhere;line-height:1.5'
			}, message.text)
		]);
	});
}

function refreshChatRows() {
	const node = document.getElementById('agent-hub-chat-history');
	if (!node)
		return;
	dom.content(node, chatRows());
	node.scrollTop = node.scrollHeight;
}

function appendChatMessage(role, text) {
	chatMessages.push({ role: role, text: text });
	refreshChatRows();
}

function setChatBusy(busy) {
	chatBusy = busy;
	const send = document.getElementById('agent-hub-chat-send');
	const input = document.getElementById('agent-hub-chat-input');
	const progress = document.getElementById('agent-hub-chat-progress');
	if (send)
		send.disabled = busy || !chatServiceRunning;
	if (input)
		input.disabled = busy || !chatServiceRunning;
	if (progress)
		dom.content(progress, busy ? _('Working...') : '');
}

function chatFailure(err) {
	if (chatTimer !== null) {
		window.clearTimeout(chatTimer);
		chatTimer = null;
	}
	setChatBusy(false);
	appendChatMessage('agent', '%s: %s'.format(_('Error'), err.message || err));
}

function pollChatJob(jobId) {
	if (!document.getElementById('agent-hub-chat-history')) {
		setChatBusy(false);
		return;
	}

	fs.exec(CHAT_COMMAND, [ 'status', jobId ]).then(commandPayload).then(function(payload) {
		if (payload.status === 'running') {
			chatTimer = window.setTimeout(function() { pollChatJob(jobId); }, 1000);
			return;
		}

		chatTimer = null;
		setChatBusy(false);
		if (payload.status === 'done')
			appendChatMessage('agent', payload.output || _('No response'));
		else
			appendChatMessage('agent', '%s: %s'.format(_('Error'), payload.output || _('Chat failed')));
	}).catch(chatFailure);
}

function submitChat() {
	const input = document.getElementById('agent-hub-chat-input');
	const message = input ? input.value.trim() : '';
	if (chatBusy || message === '')
		return;

	appendChatMessage('user', message);
	input.value = '';
	setChatBusy(true);
	fs.exec(CHAT_COMMAND, [ 'submit', message ]).then(commandPayload).then(function(payload) {
		if (!payload.job_id)
			throw new Error(payload.output || _('Chat job was not created'));
		pollChatJob(payload.job_id);
	}).catch(chatFailure);
}

function renderChatSection(status) {
	chatServiceRunning = status.running;
	return E('div', { class: 'cbi-section' }, [
		E('style', {}, '@media (max-width:600px){#agent-hub-chat-actions{margin-right:44px}#maincontent .cbi-tabmenu{width:calc(100% - 44px)}}'),
		E('h3', {}, _('Console')),
		E('div', {
			id: 'agent-hub-chat-history',
			'aria-live': 'polite',
			style: 'height:240px;overflow:auto;border:1px solid var(--border-color-medium, #2b303b);padding:12px;margin-bottom:10px'
		}, chatRows()),
		E('textarea', {
			id: 'agent-hub-chat-input',
			class: 'cbi-input-textarea',
			rows: 3,
			maxlength: 32768,
			disabled: !status.running,
			placeholder: status.running ? _('Type a message...') : _('Start Agent Hub first'),
			style: 'width:100%;box-sizing:border-box;resize:vertical',
			keydown: function(ev) {
				if ((ev.ctrlKey || ev.metaKey) && ev.key === 'Enter') {
					ev.preventDefault();
					submitChat();
				}
			}
		}),
		E('div', {
			style: 'display:flex;align-items:center;justify-content:space-between;gap:10px;margin-top:8px;flex-wrap:wrap'
		}, [
			E('span', { id: 'agent-hub-chat-progress', style: 'color:#7a7a7a' }, ''),
			E('div', { id: 'agent-hub-chat-actions', style: 'display:flex;gap:8px' }, [
				E('button', {
					class: 'btn cbi-button cbi-button-neutral',
					type: 'button',
					click: function() {
						chatMessages = [];
						refreshChatRows();
					}
				}, _('Clear')),
				E('button', {
					id: 'agent-hub-chat-send',
					class: 'btn cbi-button cbi-button-action',
					type: 'button',
					disabled: !status.running,
					click: submitChat
				}, _('Send'))
			])
		])
	]);
}

function browserHost() {
	const host = window.location.hostname;
	return host.indexOf(':') >= 0 ? '[%s]'.format(host) : host;
}

function officialWebUI() {
	const engine = uci.get('agent-hub', 'main', 'engine') || 'picoclaw';
	if (engine === 'picoclaw' && uci.get('agent-hub', 'main', 'picoclaw_web_ui') === '1' &&
	    (uci.get('agent-hub', 'main', 'picoclaw_web_scope') || 'lan') === 'lan') {
		return 'http://%s:%s/'.format(browserHost(), uci.get('agent-hub', 'main', 'picoclaw_web_port') || '18800');
	}
	if (engine === 'zeroclaw' && (uci.get('agent-hub', 'main', 'listen_scope') || 'loopback') === 'lan') {
		return 'http://%s:%s/'.format(browserHost(), uci.get('agent-hub', 'main', 'port') || '18790');
	}
	return null;
}

return view.extend({
	load() {
		return Promise.all([ uci.load('agent-hub'), getServiceStatus() ]);
	},

	render(data) {
		let m, s, o;
		const webUI = officialWebUI();

		m = new form.Map('agent-hub', _('Agent Hub'));

		s = m.section(form.TypedSection);
		s.anonymous = true;
		s.render = function() {
			poll.add(function() {
				return getServiceStatus().then(function(status) {
					chatServiceRunning = status.running;
					if (!chatBusy)
						setChatBusy(false);
					const node = document.getElementById('agent-hub-status');
					if (node)
						dom.content(node, renderStatus(status));
				});
			});

			const buttons = [];
			if (webUI) {
				buttons.push(E('button', {
					class: 'btn cbi-button cbi-button-action',
					type: 'button',
					click: function() { window.open(webUI, '_blank', 'noopener'); }
				}, _('Open Web UI')));
			}
			buttons.push(
				E('button', {
					class: 'btn cbi-button cbi-button-apply',
					type: 'button',
					click: ui.createHandlerFn(this, function() { return runServiceAction('start'); })
				}, _('Start')),
				E('button', {
					class: 'btn cbi-button cbi-button-action',
					type: 'button',
					click: ui.createHandlerFn(this, function() { return runServiceAction('restart'); })
				}, _('Restart')),
				E('button', {
					class: 'btn cbi-button cbi-button-negative',
					type: 'button',
					click: ui.createHandlerFn(this, function() { return runServiceAction('stop'); })
				}, _('Stop'))
			);

			return E('div', { class: 'cbi-section' }, [
				E('div', {
					style: 'display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap'
				}, [
					E('div', { id: 'agent-hub-status' }, renderStatus(data[1])),
					E('div', { style: 'display:flex;gap:8px;flex-wrap:wrap' }, buttons)
				])
			]);
		};

		s = m.section(form.TypedSection);
		s.anonymous = true;
		s.render = function() { return renderChatSection(data[1]); };

		s = m.section(form.NamedSection, 'main', 'agent_hub');
		s.addremove = false;
		s.tab('general', _('General'));
		s.tab('model', _('Model'));
		s.tab('network', _('Network'));
		s.tab('telegram', _('Telegram'));
		s.tab('web', _('Web UI'));
		s.tab('advanced', _('Advanced'));

		o = s.taboption('general', form.Flag, 'enabled', _('Enable'));
		o.default = o.disabled;
		o.rmempty = false;

		o = s.taboption('general', form.ListValue, 'engine', _('Runtime'));
		o.value('picoclaw', 'PicoClaw 0.3.1');
		o.value('nullclaw', 'NullClaw 2026.5.29');
		o.value('zeroclaw', 'ZeroClaw 0.8.4');
		o.default = 'picoclaw';
		o.rmempty = false;

		o = s.taboption('telegram', form.Flag, 'telegram_enabled', _('Enable Telegram bot'));
		o.default = o.disabled;
		o.rmempty = false;
		o.depends('managed_config', '1');

		o = s.taboption('telegram', form.Value, 'telegram_bot_token', _('Bot token'));
		o.password = true;
		o.placeholder = '123456789:AA...';
		o.rmempty = false;
		o.depends({ managed_config: '1', telegram_enabled: '1' });
		o.validate = function(sectionId, value) {
			return /^[0-9]+:[A-Za-z0-9_-]{20,}$/.test(value || '')
				? true : _('Enter a valid Telegram bot token');
		};

		o = s.taboption('telegram', form.DynamicList, 'telegram_allow_from', _('Allowed user IDs'));
		o.placeholder = '123456789';
		o.rmempty = false;
		o.depends({ managed_config: '1', telegram_enabled: '1' });
		o.validate = function(sectionId, value) {
			if (!value)
				return true;
			return /^[1-9][0-9]{0,19}$/.test(value || '')
				? true : _('Use a positive numeric Telegram user ID');
		};

		o = s.taboption('telegram', form.Flag, 'telegram_group_mention_only', _('Require mention in groups'));
		o.default = o.enabled;
		o.rmempty = false;
		o.depends({ managed_config: '1', telegram_enabled: '1' });

		o = s.taboption('telegram', form.Value, 'telegram_proxy', _('API proxy URL'));
		o.placeholder = 'http://127.0.0.1:7890';
		o.rmempty = true;
		o.depends({ managed_config: '1', telegram_enabled: '1' });
		o.validate = function(sectionId, value) {
			if (!value)
				return true;
			return /^(https?|socks5):\/\/[^\s]+$/.test(value)
				? true : _('Use an HTTP, HTTPS, or SOCKS5 URL');
		};

		o = s.taboption('model', form.ListValue, 'provider', _('Provider'));
		o.value('openai_compatible', _('OpenAI-compatible'));
		o.value('openai', 'OpenAI');
		o.value('openrouter', 'OpenRouter');
		o.value('anthropic', 'Anthropic');
		o.value('gemini', 'Gemini');
		o.value('deepseek', 'DeepSeek');
		o.value('groq', 'Groq');
		o.value('ollama', 'Ollama');
		o.default = 'openai_compatible';
		o.rmempty = false;

		o = s.taboption('model', form.Value, 'api_base', _('API base URL'));
		o.placeholder = 'https://api.example.com/v1';
		o.datatype = 'url';
		o.rmempty = true;

		o = s.taboption('model', form.Value, 'api_key', _('API key'));
		o.password = true;
		o.rmempty = true;

		o = s.taboption('model', form.Value, 'model', _('Model'));
		o.placeholder = 'gpt-4o-mini';
		o.rmempty = false;

		o = s.taboption('model', form.Value, 'temperature', _('Temperature'));
		o.placeholder = '0.7';
		o.datatype = 'ufloat';
		o.rmempty = true;
		o.validate = function(sectionId, value) {
			if (value === '')
				return true;
			const number = Number(value);
			return Number.isFinite(number) && number >= 0 && number <= 2
				? true : _('Must be between 0 and 2');
		};

		o = s.taboption('model', form.Value, 'max_tokens', _('Maximum output tokens'));
		o.placeholder = '4096';
		o.datatype = 'range(1,1048576)';
		o.rmempty = true;

		o = s.taboption('network', form.ListValue, 'listen_scope', _('Listen scope'));
		o.value('loopback', _('Router only'));
		o.value('lan', _('LAN'));
		o.default = 'loopback';
		o.rmempty = false;

		o = s.taboption('network', form.Value, 'port', _('Listen port'));
		o.datatype = 'port';
		o.default = '18790';
		o.rmempty = false;

		o = s.taboption('web', form.Flag, 'picoclaw_web_ui', _('PicoClaw Web UI'));
		o.default = o.disabled;
		o.rmempty = false;
		o.depends('engine', 'picoclaw');

		o = s.taboption('web', form.ListValue, 'picoclaw_web_scope', _('Web UI scope'));
		o.value('loopback', _('Router only'));
		o.value('lan', _('LAN'));
		o.default = 'lan';
		o.rmempty = false;
		o.depends({ engine: 'picoclaw', picoclaw_web_ui: '1' });

		o = s.taboption('web', form.Value, 'picoclaw_web_port', _('Web UI port'));
		o.datatype = 'port';
		o.default = '18800';
		o.rmempty = false;
		o.depends({ engine: 'picoclaw', picoclaw_web_ui: '1' });

		o = s.taboption('web', form.DummyValue, '_zeroclaw_dashboard', _('ZeroClaw Dashboard'));
		o.cfgvalue = function() { return _('Uses the runtime listen scope and port'); };
		o.depends('engine', 'zeroclaw');

		o = s.taboption('web', form.DummyValue, '_nullclaw_console', _('NullClaw'));
		o.cfgvalue = function() { return _('Use the Agent Hub console'); };
		o.depends('engine', 'nullclaw');

		o = s.taboption('advanced', form.Flag, 'managed_config', _('Use common settings'));
		o.default = o.enabled;
		o.rmempty = false;

		o = s.taboption('advanced', form.Flag, 'respawn', _('Restart after failure'));
		o.default = o.enabled;
		o.rmempty = false;

		return m.render();
	}
});
