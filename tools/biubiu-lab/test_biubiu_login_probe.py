#!/usr/bin/env python3

import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import biubiu_login_probe as probe


class SessionTests(unittest.TestCase):
    def write_session(self, path: Path) -> None:
        path.write_text(
            json.dumps(
                {
                    "local": {
                        "deviceId": "5f234ff7-cf79-492a-aa16-f7509d37dd61",
                        "method": "sms",
                    },
                    "login": {
                        "code": "SUCCESS",
                        "data": {
                            "sessionInfo": {
                                "sessionId": "private-session-value",
                                "refreshToken": "private-refresh-value",
                                "cookies": [{"name": "private-cookie"}],
                            }
                        },
                    },
                }
            )
        )

    def test_status_is_redacted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "session.json"
            self.write_session(path)
            rendered = json.dumps(probe.session_status(path))
            self.assertNotIn("private-", rendered)
            self.assertIn('"authenticated": true', rendered)
            self.assertIn('"refreshable": true', rendered)

    def test_refresh_uses_both_credentials_and_replaces_privately(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "session.json"
            self.write_session(path)
            replacement = {
                "code": "SUCCESS",
                "data": {
                    "sessionInfo": {
                        "sessionId": "replacement-session",
                        "refreshToken": "replacement-refresh",
                        "cookies": [],
                    }
                },
            }

            with mock.patch.object(probe, "request", return_value=replacement) as request:
                probe.refresh_session(path)

            endpoint, payload = request.call_args.args
            self.assertEqual(endpoint, "capi/login.autoLogin")
            self.assertEqual(payload["sessionToken"], "private-refresh-value")
            self.assertEqual(
                payload["clientUser"]["sessionId"], "private-session-value"
            )
            self.assertEqual(os.stat(path).st_mode & 0o777, 0o600)
            stored = json.loads(path.read_text())
            self.assertEqual(stored["local"]["method"], "refresh")
            self.assertEqual(stored["login"], replacement)


if __name__ == "__main__":
    unittest.main()
