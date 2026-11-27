#!/usr/bin/env python3
"""Regression tests for software instruction replacements that can be applied in place."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import isa_emulation


def main():
    # The Sandy Bridge-host failure from Uncharted 2: movbe eax, [rbx] at image offset 0x10d230.
    original = bytes.fromhex("0f38f003")
    site = isa_emulation.parse(original, 0x10d230)
    assert site is not None
    assert site.kind == "movbe_load"
    assert site.size == 32
    assert site.length == len(original)

    replacement = isa_emulation.inline_replacement(site, 0x10d230)
    assert replacement == bytes.fromhex("8b030fc8"), replacement.hex() if replacement else None
    assert len(replacement) == len(original)
    print("ISA emulation tests: in-place MOVBE load replacement passed")


if __name__ == "__main__":
    main()
