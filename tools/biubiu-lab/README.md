# biubiu protocol lab

These tools are for user-authorized interoperability testing on a development
machine. Generated QR images and session files belong in `/tmp`; never commit
them.

`biubiu_login_probe.py` is the reference probe used to validate the account
handshake before implementing it in the OpenWrt C client. It requires Python
3 and `cryptography`. A stable device UUID is retained in a mode `0600` file
so multi-step login requests use one device identity.

Phone SMS login does not require the mobile app:

```sh
python3 tools/biubiu-lab/biubiu_login_probe.py --send-sms 13800000000
python3 tools/biubiu-lab/biubiu_login_probe.py \
  --sms-login 13800000000 123456
```

Password login prompts without echoing the password or placing it in argv:

```sh
python3 tools/biubiu-lab/biubiu_login_probe.py --password-login 13800000000
```

QR login remains available where the mobile client exposes its scanner:

```sh
python3 tools/biubiu-lab/biubiu_login_probe.py --authorize
```

Successful SMS, password, or QR login writes a mode `0600` session file to
`/tmp/biubiu-session.json`. The file contains live account material and must
never be committed or attached to a bug report.
