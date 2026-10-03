#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders: Salvatore Paxia
"""Check that PR6610 paper pixels are not stretched in P6066 layouts."""

from pathlib import Path
from xml.etree import ElementTree
import argparse
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--binary", type=Path, help="also verify built MAME device screen dimensions")
args = parser.parse_args()

ROOT = Path(__file__).resolve().parents[2]
PRINTER = "bus:console:goino:options:pr6610:paper"
VIDEO = "bus:video:go011:framebuffer"
# The P6066-specific paper screen presents the shared roll at full resolution.
NATIVE_ASPECT = {PRINTER: 1736 / 1271, VIDEO: 560 / 410}

printer_config = (ROOT / "src/devices/bus/p6066/pr6610.h").read_text()
viewport = re.search(r"PAPER_SCREEN_HEIGHT\s*=\s*(\d+)", printer_config)
ring = re.search(r"PAPER_HEIGHT\s*=\s*(\d+)", printer_config)
assert viewport and int(viewport.group(1)) == 1271, "paper viewport must fill the pane at square-pixel aspect"
assert ring and int(ring.group(1)) > 1271 + 100, "paper storage ring must cover the full live viewport"
assert "set_display_reduction" not in (ROOT / "src/devices/bus/p6066/pr6610.cpp").read_text()
assert "m_display_reduction" not in (ROOT / "src/devices/machine/bitmap_printer.cpp").read_text()
assert "preview_channel" not in (ROOT / "src/devices/machine/bitmap_printer.cpp").read_text()
driver = (ROOT / "src/mame/olivetti/p6066.cpp").read_text()
assert "request_window_size" not in driver
assert "set_dynamic_interactive_bounds" not in driver
assert "target->set_view(index)" in driver
assert "select_initial_output_view()" not in driver, "selection after window creation sizes for the wrong view"

for filename in ("p6066_printer.lay", "p6066_video_printer.lay"):
    tree = ElementTree.parse(ROOT / "src/mame/layout" / filename)
    groups = {group.get("name"): group for group in tree.findall("./group")}
    view_names = [view.get("name") for view in tree.findall("./view")]
    console = groups["console"]
    if filename == "p6066_video_printer.lay":
        controls = groups["output_controls"]
        for label in ("kbmode_label", "disk_label0", "disk_label1", "disk_label2", "disk_label3", "footer"):
            bounds = console.find(f"./element[@ref='{label}']/bounds")
            assert bounds is not None and float(bounds.get("height")) == 2.5, (
                f"{label}: lower console lettering must be readable alongside the host controls")
        assert float(console.find("./element[@ref='kbmode_label']/bounds").get("width")) == 18
        assert float(console.find("./element[@ref='footer']/bounds").get("width")) == 82
        assert {
            (item.get("inputtag"), item.get("inputmask"))
            for item in controls.findall("./element") if item.get("inputtag")
        } == {
            ("OUTPUT_VIEW", mask) for mask in ("0x01", "0x02", "0x04")
        } | {("bus:console:goino:options:pr6610:MANUAL_FEED", "0x01")}
        assert console.find("./group[@ref='output_controls']") is not None
        default = tree.find("./view[@name='Video, Printer and Console']")
        assert default is not None
        hidden = default.find("./screen[@tag='bus:console:goino:options:pr6610:bitmap:screen']")
        assert hidden is not None and hidden.find("color").get("alpha") == "0", (
            "auto view must include all screens without showing the shared printer overlays")
        buttons = [item.find("bounds") for item in controls.findall("./element") if item.get("inputtag")]
        assert [(float(b.get("x")), float(b.get("y")), float(b.get("width")), float(b.get("height")))
                for b in buttons] == [
                    (4, 2, 16, 5), (22, 2, 16, 5), (40, 2, 16, 5), (92, 2, 16, 5)
                ], "host buttons belong above the LCD, with FEED on the right"
        source = (ROOT / "src/mame/olivetti/p6066.cpp").read_text()
        native = console.find("bounds")
        native_ratio = float(native.get("width")) / float(native.get("height"))
        for selected in ("Video and Console", "Printer and Console", "Video, Printer and Console"):
            assert f'"{selected}"' in source, f"selector must target the {selected} layout view"
            view = next(v for v in tree.findall("./view") if v.get("name") == selected)
            panel = view.find("./group[@ref='console']/bounds")
            assert panel is not None, f"{selected}: missing console"
            layout_width = 200 if selected == "Video, Printer and Console" else 100
            assert (float(panel.get("width")), float(panel.get("height"))) == (89.6, 35.2), (
                f"{selected}: changing output must not scale the console")
            assert abs(float(panel.get("width")) / float(panel.get("height")) - native_ratio) < 0.001, (
                f"{selected}: console must keep its native aspect ratio")
            assert abs(float(panel.get("x")) + float(panel.get("width")) / 2 - layout_width / 2) < 0.001, (
                f"{selected}: console should be horizontally centered")
        assert "if (target->view() != index)" in source, (
            "re-selecting a view must not alter the current view")
    else:
        default = tree.find("./view[@name='Console and Printer']")
        hidden = default.find("./screen[@tag='bus:console:goino:options:pr6610:bitmap:screen']")
        assert hidden is not None and hidden.find("color").get("alpha") == "0"
        controls = groups["feed_control"]
        assert [(item.get("inputtag"), item.get("inputmask"))
                for item in controls.findall("./element") if item.get("inputtag")] == [
                    ("bus:console:goino:options:pr6610:MANUAL_FEED", "0x01")]
        assert console.find("./group[@ref='feed_control']") is not None
        assert controls.find("./element[@inputtag]").find("bounds").get("width") == "16"
        assert tree.find("./view[@name='Printer']/group[@ref='feed_control']") is not None
        assert not tree.findall(".//*[@inputtag='OUTPUT_VIEW']")
    seen = set()
    for screen in tree.findall(".//view/screen"):
        tag = screen.get("tag")
        if tag not in NATIVE_ASPECT:
            continue
        bounds = screen.find("bounds")
        ratio = float(bounds.get("width")) / float(bounds.get("height"))
        assert abs(ratio / NATIVE_ASPECT[tag] - 1) < 0.001, (
            f"{filename}: {tag} displayed at aspect {ratio:.3f}, "
            f"native aspect {NATIVE_ASPECT[tag]:.3f}"
        )
        seen.add(tag)
    assert PRINTER in seen, f"{filename}: no printer screen"
    if filename == "p6066_video_printer.lay":
        assert VIDEO in seen, f"{filename}: no video screen"

for filename in ("p6066.lay", "p6066_video.lay"):
    layout = ElementTree.parse(ROOT / "src/mame/layout" / filename)
    assert not layout.findall(".//*[@inputtag='OUTPUT_VIEW']")
    assert not layout.findall(".//*[@inputtag='bus:console:goino:options:pr6610:MANUAL_FEED']")

if args.binary:
    for device, expected_tag, expected_height in (
        ("p6066_pr6610", ":paper", 1271),
        ("p6066_pr6610", ":bitmap:screen", 384),
        ("bitmap_printer", ":screen", 384),
    ):
        xml = subprocess.run(
            [str(args.binary.resolve()), "-listxml", device],
            check=True, capture_output=True, text=True,
        ).stdout
        machine = ElementTree.fromstring(xml).find(f"./machine[@name='{device}']")
        screen = machine.find(f"./display[@tag='{expected_tag}']")
        assert int(screen.get("height")) == expected_height, (
            f"{device}: built screen height is {screen.get('height')}, "
            f"expected {expected_height}"
        )

print("PASS: PR6610/GO011 pixel aspect, driver-only view switching and output-dependent controls")
