"""Regression tests for the conservative D-Checker LDD -> C++ generator."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from ldd_to_header import load_rows, make_definitions, render_header


class LddProfileTests(unittest.TestCase):
    def test_real_ldd_csv_fixture(self):
        source = ROOT / "tests" / "fixtures" / "ldd_lt_da_sample.csv"
        definitions, skipped = make_definitions(load_rows(source), limit=64)
        self.assertEqual(len(definitions), 11)
        self.assertEqual(definitions[0], ("LDD Outdoor air temp.(R1T)", 0x20, 0, 2, 105, 1))
        self.assertEqual(definitions[4], ("LDD Voltage (N-phase) (V)", 0x21, 4, 2, 101, 0))
        self.assertGreater(skipped["wildcard/invalid register"], 0)
        self.assertGreater(skipped["unsupported converter"], 0)
        generated = render_header(definitions, source.name)
        committed = (ROOT / "components" / "altherma_hub" / "ldd_generated_profile.h").read_text(encoding="utf-8")
        self.assertEqual(generated, committed)

    def test_discard_commands_hidden_and_out_of_bounds(self):
        template = dict(DataId="0x20", Offset="0", Bytes="2", ConvId="105",
                        Enabled="True", IsDisplay="True", LabelStr="Safe Temperature",
                        FormatStr="%.1f", DataKind="A")
        cases = [
            dict(DataId="0x3*"),
            dict(LabelStr="Override CMD:0x00"),
            dict(Enabled="False"),
            dict(IsDisplay="False"),
            dict(Offset="63"),
            dict(Bytes="0"),
            dict(ConvId="217"),
            dict(DataKind="W"),
        ]
        values, _ = make_definitions([template, *[dict(template, **x) for x in cases]], limit=64)
        self.assertEqual(len(values), 1)

    def test_duplicate_names_get_distinct_object_ids(self):
        source = dict(DataId="0x20", Offset="0", Bytes="2", ConvId="105",
                      Enabled="True", IsDisplay="True", LabelStr="Test",
                      FormatStr="%.1f", DataKind="A")
        rows = [source, dict(source, Offset="2")]
        values, _ = make_definitions(rows, limit=64)
        self.assertEqual(len(values), 2)
        self.assertNotEqual(values[0][0], values[1][0])

    def test_limit_refuses_truncation(self):
        source = dict(DataId="0x20", Offset="0", Bytes="2", ConvId="105",
                      Enabled="True", IsDisplay="True", LabelStr="Test",
                      FormatStr="%.1f", DataKind="A")
        with self.assertRaisesRegex(ValueError, "More than 1"):
            make_definitions([source, dict(source, Offset="2")], limit=1)


if __name__ == "__main__":
    unittest.main()
