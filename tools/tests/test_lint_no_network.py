"""Unit tests for the no-network check of tools/lint.py. Run: python3 -I tools/tests/test_lint_no_network.py"""

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location(
    "lint", Path(__file__).resolve().parent.parent / "lint.py"
)
assert _SPEC is not None and _SPEC.loader is not None
lint = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(lint)


class NoNetworkTest(unittest.TestCase):
    def check(self, text: str) -> list[str]:
        with tempfile.TemporaryDirectory(dir=lint.REPO) as directory:
            path = Path(directory) / "x.cpp"
            path.write_text(text, encoding="utf-8")
            return lint.find_network_uses([path])

    def test_network_apis_are_found(self) -> None:
        for text in (
            "QNetworkAccessManager manager;",
            "auto* s = new QTcpSocket(this);",
            "#include <winsock2.h>",
            "#include <sys/socket.h>",
            "WSAStartup(MAKEWORD(2, 2), &data);",
            "HINTERNET h = WinHttpOpen(nullptr, 0, nullptr, nullptr, 0);",
        ):
            with self.subTest(text=text):
                self.assertEqual(len(self.check(text)), 1)

    def test_local_ipc_and_comments_are_allowed(self) -> None:
        self.assertEqual(self.check("QLocalServer server; QLocalSocket socket;"), [])
        self.assertEqual(self.check("// QTcpSocket is not used (F-ROB-4)"), [])

    def test_translations_are_found(self) -> None:
        with tempfile.TemporaryDirectory(dir=lint.REPO) as directory:
            path = Path(directory) / "x.cpp"
            for text, count in (("QTranslator translator;", 1), ('label->setText(tr("Open"));', 1),
                                ("app.installTranslator(&t);", 1), ("int str(int);", 0),
                                ("const auto ptr(nullptr);", 0)):
                with self.subTest(text=text):
                    path.write_text(text, encoding="utf-8")
                    self.assertEqual(len(lint.find_translation_uses([path])), count)

    def test_the_sources_are_clean(self) -> None:
        self.assertEqual(lint.run_no_network(), 0)


if __name__ == "__main__":
    unittest.main()
