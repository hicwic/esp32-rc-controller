#!/usr/bin/env python3
import argparse
import glob
import json
import os


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets-root", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--repo", required=True)
    args = parser.parse_args()

    meta_files = glob.glob(os.path.join(args.assets_root, "**", "*-meta.json"), recursive=True)
    if not meta_files:
        raise RuntimeError("No metadata files found in downloaded artifacts.")

    for meta_path in meta_files:
        with open(meta_path, "r", encoding="utf-8") as f:
            meta = json.load(f)

        env = meta["env"]
        chip = meta["chip_family"]
        files = meta["files"]
        offsets = meta["offsets"]

        def asset_url(filename: str) -> str:
            return f"https://github.com/{args.repo}/releases/download/{args.tag}/{filename}"

        manifest = {
            "name": f"RC Controller ({env})",
            "version": args.tag,
            "new_install_prompt_erase": True,
            "builds": [
                {
                    "chipFamily": chip,
                    "parts": [
                        {"path": asset_url(files["bootloader"]), "offset": offsets["bootloader"]},
                        {"path": asset_url(files["partitions"]), "offset": offsets["partitions"]},
                        {"path": asset_url(files["firmware"]), "offset": offsets["firmware"]},
                        {"path": asset_url(files["filesystem"]), "offset": offsets["filesystem"]},
                    ],
                }
            ],
        }

        manifest_name = f"manifest-{env}.json"
        out_path = os.path.join(os.path.dirname(meta_path), manifest_name)
        with open(out_path, "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2)


if __name__ == "__main__":
    main()

