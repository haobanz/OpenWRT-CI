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

function getServiceStatus() {
	return L.resolveDefault(callServiceList('agent-hub'), {}).then(function(res) {
		const instances = res['agent-hub'] && res['agent-hub'].instances || {};
		for (const name in instances) {
			if (instances[name].running)
				return { running: true, engine: name };
		}
		return { running: false, engine: null };
	});
}

function renderStatus(status) {
	const color = status.running ? '#16813d' : '#7a7a7a';
	const label = status.running
		? '%s: %s'.format(_('Running'), status.engine)
		: _('Stopped');

	return E('span', {
		style: 'color:%s;font-weight:600'.format(color)
	}, label);
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

return view.extend({
	load() {
		return Promise.all([ uci.load('agent-hub'), getServiceStatus() ]);
	},

	render(data) {
		let m, s, o;

		m = new form.Map('agent-hub', _('Agent Hub'));

		s = m.section(form.TypedSection);
		s.anonymous = true;
		s.render = function() {
			poll.add(function() {
				return getServiceStatus().then(function(status) {
					const node = document.getElementById('agent-hub-status');
					if (node)
						dom.content(node, renderStatus(status));
				});
			});

			return E('div', { class: 'cbi-section' }, [
				E('div', {
					style: 'display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap'
				}, [
					E('div', { id: 'agent-hub-status' }, renderStatus(data[1])),
					E('div', { style: 'display:flex;gap:8px;flex-wrap:wrap' }, [
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
					])
				])
			]);
		};

		s = m.section(form.NamedSection, 'main', 'agent_hub');
		s.addremove = false;
		s.tab('general', _('General'));
		s.tab('model', _('Model'));
		s.tab('network', _('Network'));
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

		o = s.taboption('advanced', form.Flag, 'managed_config', _('Use common settings'));
		o.default = o.enabled;
		o.rmempty = false;

		o = s.taboption('advanced', form.Flag, 'respawn', _('Restart after failure'));
		o.default = o.enabled;
		o.rmempty = false;

		return m.render();
	}
});
