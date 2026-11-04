import importlib.util
import os
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock


SCRIPT_PATH = Path(__file__).resolve().parents[2] / "scripts" / "derive-version.py"
SPEC = importlib.util.spec_from_file_location("derive_version", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
derive_version = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(derive_version)


class DeriveVersionTests(unittest.TestCase):
    def derive_from_git(self, describe: str) -> dict[str, str]:
        def run_git(_source_root: Path, *args: str) -> str:
            if args and args[0] == "describe":
                return describe
            if args == ("rev-parse", "--short=8", "HEAD"):
                return "feedface"
            raise AssertionError(f"Unexpected git command: {args}")

        with (
            mock.patch.dict(os.environ, {"CI_VERSION": ""}),
            mock.patch.object(derive_version, "run_git", side_effect=run_git),
            mock.patch.object(
                derive_version, "read_fallback_version", return_value="6.2.84"
            ),
        ):
            return derive_version.derive(Path("."))

    def test_exact_stable_tag(self) -> None:
        result = self.derive_from_git("v6.3.13-0-g12345678")

        self.assertEqual(result["display"], "6.3.13")
        self.assertEqual(result["artifact"], "6.3.13")
        self.assertEqual(result["numeric"], "6.3.13.0")

    def test_exact_legacy_dot_prerelease_tag(self) -> None:
        result = self.derive_from_git("v6.3.10.beta-0-g4af38f30")

        self.assertEqual(result["display"], "6.3.10.beta")
        self.assertEqual(result["artifact"], "6.3.10.beta")
        self.assertEqual(result["numeric"], "6.3.10.0")

    def test_exact_semver_prerelease_tag(self) -> None:
        result = self.derive_from_git("v6.3.14-beta.1-0-gabcdef12")

        self.assertEqual(result["display"], "6.3.14-beta.1")
        self.assertEqual(result["artifact"], "6.3.14-beta.1")
        self.assertEqual(result["numeric"], "6.3.14.0")

    def test_commits_after_prerelease_tag_keep_git_metadata(self) -> None:
        result = self.derive_from_git(
            "v6.3.14-beta.1-3-gabcdef123456-dirty"
        )

        self.assertEqual(
            result["display"], "6.3.14-beta.1+3.gabcdef12.dirty"
        )
        self.assertEqual(
            result["artifact"], "6.3.14-beta.1-3.gabcdef12.dirty"
        )
        self.assertEqual(result["numeric"], "6.3.14.3")

    def test_explicit_version_strips_tag_prefix(self) -> None:
        with (
            mock.patch.dict(os.environ, {"CI_VERSION": "v6.3.15-rc.1"}),
            mock.patch.object(derive_version, "run_git", return_value="feedface"),
            mock.patch.object(
                derive_version, "read_fallback_version", return_value="6.2.84"
            ),
        ):
            result = derive_version.derive(Path("."))

        self.assertEqual(result["display"], "6.3.15-rc.1")
        self.assertEqual(result["artifact"], "6.3.15-rc.1")
        self.assertEqual(result["numeric"], "6.3.15.0")

    def test_release_version_has_numeric_platform_version(self) -> None:
        with (
            mock.patch.dict(os.environ, {"CI_VERSION": "1.0.0"}),
            mock.patch.object(derive_version, "run_git", return_value="feedface"),
        ):
            result = derive_version.derive(Path("."))
        self.assertEqual(result["display"], "1.0.0")
        self.assertEqual(result["artifact"], "1.0.0")
        self.assertEqual(result["numeric"], "1.0.0.0")
        self.assertEqual(result["windows_numeric"], "7.0.0.0")

    def test_windows_release_versions_upgrade_6_x_and_keep_release_order(self) -> None:
        previous = (6, 4, 1)
        for version, expected in (
            ("1.0.0", "7.0.0.0"),
            ("1.0.1", "7.0.1.0"),
            ("1.1.0", "7.1.0.0"),
            ("2.0.0", "8.0.0.0"),
        ):
            with (
                self.subTest(version=version),
                mock.patch.dict(os.environ, {"CI_VERSION": version}),
                mock.patch.object(derive_version, "run_git", return_value="feedface"),
            ):
                result = derive_version.derive(Path("."))
                self.assertEqual(result["display"], version)
                self.assertEqual(result["artifact"], version)
                self.assertEqual(result["numeric"], version + ".0")
                self.assertEqual(result["windows_numeric"], expected)
                # Windows Installer compares only the first three components.
                installer_version = tuple(int(part) for part in expected.split(".")[:3])
                self.assertGreater(installer_version, previous)
                previous = installer_version

    def test_windows_git_tag_version_preserves_metadata_and_revision(self) -> None:
        for count in (0, 3, 70000):
            with self.subTest(count=count):
                result = self.derive_from_git(f"v1.2.3-beta.1-{count}-gabcdef123456-dirty")
                self.assertEqual(result["numeric"], f"1.2.3.{min(count, 65535)}")
                self.assertEqual(result["windows_numeric"], f"7.2.3.{min(count, 65535)}")
                self.assertEqual(result["display"], f"1.2.3-beta.1+{count}.gabcdef12.dirty")
                self.assertEqual(result["artifact"], f"1.2.3-beta.1-{count}.gabcdef12.dirty")

    def test_windows_exact_release_tag(self) -> None:
        result = self.derive_from_git("v1.0.0-0-g12345678")
        self.assertEqual(result["display"], "1.0.0")
        self.assertEqual(result["numeric"], "1.0.0.0")
        self.assertEqual(result["windows_numeric"], "7.0.0.0")

    def test_windows_version_cli_and_exports(self) -> None:
        for arguments, expected_lines in (
            (["--field", "windows-numeric"], ["7.0.0.0"]),
            (["--format", "env"], [
                "MOONLIGHT_DISPLAY=1.0.0",
                "MOONLIGHT_ARTIFACT=1.0.0",
                "MOONLIGHT_NUMERIC=1.0.0.0",
                "MOONLIGHT_WINDOWS_NUMERIC=7.0.0.0",
            ]),
            (["--format", "qmake"], [
                "MOONLIGHT_VERSION = 1.0.0",
                "MOONLIGHT_ARTIFACT_VERSION = 1.0.0",
                "MOONLIGHT_NUMERIC_VERSION = 1.0.0.0",
                "MOONLIGHT_WINDOWS_NUMERIC_VERSION = 7.0.0.0",
            ]),
        ):
            with self.subTest(arguments=arguments):
                output = subprocess.check_output(
                    [sys.executable, str(SCRIPT_PATH), *arguments],
                    env={**os.environ, "CI_VERSION": "1.0.0"},
                    text=True,
                ).splitlines()
                for line in expected_lines:
                    self.assertIn(line, output)

    def test_source_archive_uses_version_file_without_git(self) -> None:
        with (
            mock.patch.dict(os.environ, {"CI_VERSION": ""}),
            mock.patch.object(derive_version, "run_git", return_value=None),
            mock.patch.object(derive_version, "read_fallback_version", return_value="1.0.0"),
        ):
            result = derive_version.derive(Path("."))
        self.assertEqual(result["display"], "1.0.0")
        self.assertEqual(result["numeric"], "1.0.0.0")
        self.assertEqual(result["windows_numeric"], "7.0.0.0")


if __name__ == "__main__":
    unittest.main()
