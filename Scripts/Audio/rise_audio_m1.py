#!/usr/bin/env python3
"""Rise audio M1: bounded bank inventory, wwiser orchestration and audition WAVs.

No Unreal imports, gameplay/Content writes, tool downloads or game-directory writes.
Raw outputs stay under project/Saved. 'publish' exports metadata JSON to docs/reference.
Existing bank copies must match their sources.
Uses wwiser for HIRC/TXTP and vgmstream for decoding; does not implement a codec.
"""

from __future__ import annotations

import argparse
from array import array
from collections import Counter
import hashlib
import html
import json
import math
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
import wave
import xml.etree.ElementTree as ET

PROJECT = Path(__file__).resolve().parents[2]
DEFAULT_BANKS = (
    "Init", "pl_wp_i_gla_com", "pl_wp_i_gla_com_media",
    "pl_wp_i_gla_epv_madia", "pl_wp_i_gla_epv_khk_madia",
    "pl_wp_i_gla_oneoff_110_media",
    "pl_wp_ig_ins_com", "pl_wp_ig_ins_com_media",
    "pl_wp_ig_ins_oneoff_001_media", "pl_wp_com", "pl_wp_com_media",
    "pl_wp_ig_ins_oneoff_006_media",
    "hit_com", "hit_com_media", "hit_em", "hit_em_media",
    "hit_em_prop", "hit_em_prop_media", "hit_em_prop_khk", "hit_em_prop_khk_media",
)
GROUPS = ("pl_wp_i_gla_com", "pl_wp_ig_ins_com", "hit_com", "hit_em", "pl_wp_com")


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def bank_index(data: bytes) -> dict:
    """Index standard little-endian BKHD chunks; reject malformed DIDX ranges."""
    if data[:4] != b"BKHD":
        raise ValueError("not a BKHD bank")
    chunks = {}
    offset = 0
    while offset < len(data):
        if len(data) - offset < 8:
            raise ValueError("truncated bank chunk header")
        tag, size = struct.unpack_from("<4sI", data, offset)
        start, end = offset + 8, offset + 8 + size
        if end > len(data) or tag in chunks:
            raise ValueError("out-of-range or duplicate bank chunk")
        chunks[tag] = (start, size)
        offset = end
    head, head_size = chunks[b"BKHD"]
    if head_size < 8:
        raise ValueError("short BKHD")
    version, bank_id = struct.unpack_from("<II", data, head)
    media = []
    if b"DIDX" in chunks:
        index_start, index_size = chunks[b"DIDX"]
        if index_size % 12 or b"DATA" not in chunks:
            raise ValueError("invalid DIDX or missing DATA")
        data_start, data_size = chunks[b"DATA"]
        for pos in range(index_start, index_start + index_size, 12):
            media_id, relative, size = struct.unpack_from("<III", data, pos)
            if relative + size > data_size:
                raise ValueError("DIDX media outside DATA")
            media.append({"media_id": media_id, "offset": data_start + relative, "size": size})
    return {"version": version, "bank_id": bank_id,
            "chunks": {tag.decode("ascii"): size for tag, (_, size) in chunks.items()}, "media": media}


def prepare(source: Path, work: Path) -> list[dict]:
    rows = []
    for stem in DEFAULT_BANKS:
        original = source / "Sound/Wwise" / (stem + ".bnk.2.X64")
        target = work / "Banks" / (stem + ".bnk")
        fingerprint = sha256(original)
        if target.exists() and sha256(target) != fingerprint:
            raise ValueError(f"existing bank differs; refusing overwrite: {target}")
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original, target)
        rows.append({"source": original.as_posix(), "copy": target.as_posix(),
                     "sha256": fingerprint, "bytes": original.stat().st_size,
                     **bank_index(target.read_bytes())})
    write_json(work / "Inventory/banks.json", rows)
    return rows


def source_media_index(source: Path, work: Path) -> dict:
    """Only IG families + selected common/hit banks, never unpack the whole game."""
    lookup = {}
    root = source / "Sound/Wwise"
    paths = {path for pattern in ("pl_wp_i_gla*media.bnk.2.X64", "pl_wp_ig_ins*media.bnk.2.X64")
             for path in root.glob(pattern)}
    paths.update(root / (stem + ".bnk.2.X64") for stem in DEFAULT_BANKS)
    scanned = []
    for path in sorted(paths):
        index = bank_index(path.read_bytes())
        copied = (work / "Banks" / path.name.removesuffix(".2.X64")).is_file()
        fingerprint = sha256(path)
        scanned.append({"source": path.as_posix(), "sha256": fingerprint,
                        "bank_id": index["bank_id"], "media_count": len(index["media"]), "copied": copied})
        for item in index["media"]:
            lookup.setdefault(str(item["media_id"]), []).append(
                {"source": path.as_posix(), "source_sha256": fingerprint, "copied": copied, **item})
    write_json(work / "Inventory/media_locations.json", {"banks": scanned, "media": lookup})
    return lookup


def run_logged(args: list[str], log: Path, cwd: Path | None = None) -> None:
    result = subprocess.run(args, capture_output=True, timeout=90, cwd=cwd)
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_bytes(result.stdout + b"\nSTDERR:\n" + result.stderr)
    if result.returncode or b"Traceback (most recent call last)" in result.stdout + result.stderr:
        raise RuntimeError(f"tool failed ({result.returncode}); see {log}")


def parse_banks(work: Path, wwiser: Path) -> None:
    banks = [stem + ".bnk" for stem in DEFAULT_BANKS]
    # Fresh output prevents stale variants left by a previous bank/root configuration.
    run_dir = work / "Events" / ("TXTP-" + str(time.time_ns()))
    run_logged([sys.executable, str(wwiser), "-d", "xml", "-dn", str(work / "Events/banks"), *banks],
               work / "Inventory/wwiser_parse.log", cwd=work / "Banks")
    # wwiser concatenates root + output; its TXTP output must be relative to bank cwd.
    run_logged([sys.executable, str(wwiser), "-d", "none", "-g", "-gu", "-go",
                "../Events/" + run_dir.name, *banks], work / "Inventory/wwiser_generate.log", cwd=work / "Banks")
    write_json(work / "Inventory/parse_run.json", {"txtp_dir": run_dir.as_posix()})


def render_dependencies(path: Path, lines: list[str]) -> list[dict]:
    dependencies = []
    for line in lines:
        match = re.match(r"(.*?\.(?:bnk|wem))(?=\s|$)", line.strip())
        if match:
            target = (path.parent / match.group(1)).resolve()
            dependencies.append({"path": target.as_posix(), "exists": target.is_file()})
    return dependencies


def txtp_conditions(body: str, filename: str) -> dict:
    """Read selected header values, not the PATH/all-possible-state comments.

    wwiser truncates long filenames, but keeps complete selections in its
    gamesyncs/statechunks header. An explicitly empty header is authoritative.
    """
    headers = dict(re.findall(r"^\s*#\s*\*\s*(gamesyncs|statechunks):[ \t]*([^\r\n]*)", body, re.M))
    gamesyncs = re.findall(r"\[[^\]]+\]|\([^\)]+\)", headers['gamesyncs']) if 'gamesyncs' in headers else re.findall(r"\[[^\]]+\]", Path(filename).stem)
    states = (['{s}=' + value for value in re.findall(r"\([^\)]+\)", headers['statechunks'])]
              if 'statechunks' in headers else re.findall(r"\{s\}=\([^\)]+\)", Path(filename).stem))
    return {'conditions': list(dict.fromkeys(gamesyncs + states)),
            'gamesync_conditions': gamesyncs, 'state_conditions': states,
            'condition_sources': {'gamesyncs': 'txtp_header' if 'gamesyncs' in headers else 'filename_fallback',
                                  'statechunks': 'txtp_header' if 'statechunks' in headers else 'filename_fallback'}}


def describe_txtp(path: Path, locations: dict) -> dict:
    """Shared metadata reader for M1 and capture-selected, wwiser-authored TXTPs."""
    body = path.read_text(encoding="utf-8-sig")
    event = re.search(r"#\s+CAkEvent\[\d+\]\s+(\d+)", body)
    lines = [line for line in body.splitlines() if line.strip() and not line.lstrip().startswith("#")]
    media_ids = sorted({int(n) for n in re.findall(r"#\s+Source (\d+)", body)})
    return {"file": path.as_posix(), "bank": path.name.split("-", 1)[0],
            "event_id": int(event.group(1)) if event else None, "media_ids": media_ids,
            "media_locations": {str(mid): locations.get(str(mid), []) for mid in media_ids},
            **txtp_conditions(body, path.name),
            "render_lines": lines, "dependencies": render_dependencies(path, lines),
            "semantic_status": "candidate; action/surface mapping not confirmed"}


def refreshed_preview(item: dict) -> dict:
    """Refresh only metadata from the original TXTP; never reselect/redecode."""
    original = Path(item.get('original_txtp', item.get('file', '')))
    current = describe_txtp(original, {})
    if current['event_id'] != item['event_id'] or current['media_ids'] != item['media_ids']:
        raise ValueError('Preview identity/media changed; metadata-only refresh refused')
    if sha256(Path(item['wav'])) != item['sha256']:
        raise ValueError('Existing preview changed; metadata-only refresh refused')
    return {**item, **{key: current[key] for key in
                       ('conditions', 'gamesync_conditions', 'state_conditions', 'condition_sources')}}


def audit_events(work: Path) -> dict:
    text = (work / "Events/banks.xml").read_text(encoding="utf-8")
    # wwiser concatenates bank <root> elements without an enclosing document node.
    document = ET.fromstring("<banks>" + text + "</banks>")
    banks, nodes = [], []
    for root in document.findall("root"):
        objects = root.findall("./object[@name='HircChunk']/list/object")
        counts = Counter(obj.get("name") for obj in objects)
        banks.append({"bank": root.get("filename"), "hirc_types": dict(counts)})
        for obj in objects:
            identity = obj.find("./field[@name='ulID']")
            if identity is None:
                continue
            nodes.append({"bank": root.get("filename"), "type": obj.get("name"),
                          "index": int(obj.get("index", "0")), "id": int(identity.get("value")),
                          "name": identity.get("hashname"),
                          "references": [{"field": f.get("name"), "id": int(f.get("value"))}
                                         for f in obj.iter("field")
                                         if f.get("type") == "tid" and f.get("value", "").isdigit()],
                          "media_ids": sorted({int(f.get("value")) for f in obj.iter("field")
                                               if f.get("name") == "sourceID"})})
    write_json(work / "Inventory/hirc.json", {"banks": banks, "nodes": nodes})
    variants = []
    locations = json.loads((work / "Inventory/media_locations.json").read_text(encoding="utf-8"))["media"]
    run = json.loads((work / "Inventory/parse_run.json").read_text(encoding="utf-8"))
    for path in sorted(Path(run["txtp_dir"]).glob("*.txtp")):
        variants.append(describe_txtp(path, locations))
    write_json(work / "Inventory/variants.json", variants)
    return {"banks": len(banks), "nodes": len(nodes),
            "events": sum(n["type"] == "CAkEvent" for n in nodes), "variants": len(variants),
            "dependency_complete_variants": sum(all(d["exists"] for d in v["dependencies"]) for v in variants),
            "by_bank": dict(Counter(v["bank"] for v in variants))}


def wav_metrics(path: Path) -> dict:
    with wave.open(str(path), "rb") as stream:
        channels, rate, width = stream.getnchannels(), stream.getframerate(), stream.getsampwidth()
        count = stream.getnframes()
        payload = stream.readframes(count)
    if width != 2 or not count:
        raise ValueError("preview must contain PCM16 samples")
    values = array("h")
    values.frombytes(payload)
    if sys.byteorder != "little":
        values.byteswap()
    peak = max(abs(value) for value in values)
    rms = math.sqrt(sum(value * value for value in values) / len(values))
    return {"sample_rate": rate, "channels": channels, "seconds": count / rate,
            "peak": peak, "rms_dbfs": 20 * math.log10(max(rms, 1e-9) / 32768),
            "non_silent": peak > 0, "sha256": sha256(path)}


def export_media_slice(location: dict, media_id: int, output: Path) -> dict:
    """Copy an indexed DIDX payload with fresh bounds/hash checks, never decode it."""
    original = Path(location['source'])
    data = original.read_bytes()
    if location.get('source_sha256') and hashlib.sha256(data).hexdigest() != location['source_sha256']:
        raise ValueError('Media source hash changed')
    actual = next((row for row in bank_index(data)['media'] if row['media_id'] == media_id), None)
    if actual is None or (actual['offset'], actual['size']) != (location['offset'], location['size']):
        raise ValueError('Media index does not match source DIDX')
    payload = data[actual['offset']:actual['offset'] + actual['size']]
    fingerprint = hashlib.sha256(payload).hexdigest()
    if output.exists() and sha256(output) != fingerprint:
        raise ValueError(f'refusing to replace differing WEM: {output}')
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists():
        output.write_bytes(payload)
    return {**location, 'media_id': media_id, 'wem': output.as_posix(), 'wem_sha256': fingerprint}


def audition(work: Path, decoder: Path, per_group: int) -> list[dict]:
    variants = json.loads((work / "Inventory/variants.json").read_text(encoding="utf-8"))
    previews, failures = [], []
    for group in GROUPS:
        selected = 0
        group_failures = 0
        seen_events = set()
        for item in (v for v in variants if v["bank"] == group):
            if item["event_id"] in seen_events:
                continue
            if any(not dependency["exists"] for dependency in item["dependencies"]):
                continue
            output = work / "Audition" / group / (Path(item["file"]).stem + ".wav")
            output.parent.mkdir(parents=True, exist_ok=True)
            try:
                # JSON metadata and a finite decode; no default two loops + ten-second fade.
                meta_run = subprocess.run([str(decoder), "-I", "-m", item["file"]],
                                          capture_output=True, timeout=15, check=True)
                metadata = json.loads(meta_run.stdout.decode("utf-8"))
                seconds = metadata["numberOfSamples"] / metadata["sampleRate"]
                if seconds > 12:
                    continue
                run_logged([str(decoder), "-i", "-o", str(output), item["file"]],
                           work / "Audition/Logs" / (output.stem + ".log"))
                metrics = wav_metrics(output)
                if not metrics["non_silent"]:
                    raise ValueError("silent candidate")
                previews.append({**item, "wav": output.as_posix(), "decoder_metadata": metadata, **metrics})
                seen_events.add(item["event_id"])
                selected += 1
            except (subprocess.SubprocessError, ValueError, OSError, RuntimeError) as error:
                failures.append({"file": item["file"], "error": str(error),
                                 "stderr": getattr(error, "stderr", b"").decode("utf-8", errors="replace")[:500]})
                group_failures += 1
            if selected >= per_group:
                break
            if group_failures >= 4:
                break
    write_json(work / "Inventory/previews.json", previews)
    write_json(work / "Inventory/render_failures.json", failures)
    exported = []
    media_ids = {mid for item in previews for mid in item["media_ids"]}
    locations = json.loads((work / "Inventory/media_locations.json").read_text(encoding="utf-8"))["media"]
    for mid in sorted(media_ids):
        # Export the actual bounded DIDX slices, not a second implementation of Vorbis.
        for location in (entry for entry in locations[str(mid)] if entry["copied"]):
            original = Path(location["source"])
            stem = original.name.removesuffix(".bnk.2.X64")
            output = work / "Media" / stem / (str(mid) + ".wem")
            exported.append(export_media_slice(location, mid, output))
    write_json(work / "Inventory/exported_media.json", exported)
    return previews


def page(work: Path, previews: list[dict]) -> None:
    rows = []
    for item in previews:
        name = html.escape(Path(item["wav"]).stem)
        url = html.escape(Path(item["wav"]).relative_to(work / "Audition").as_posix(), quote=True)
        rows.append(f"<tr><td>{html.escape(item['bank'])}<br>{name}</td>"
                    f"<td>Event {item['event_id']}<br>Media {item['media_ids']}</td>"
                    f"<td>{item['seconds']:.3f}s<br>{item['sample_rate']}Hz / {item['channels']}ch</td>"
                    f'<td><audio controls preload="none" src="{url}"></audio></td></tr>')
    (work / "Audition/index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Rise IG M1试听候选</title>'
        '<style>body{font:16px sans-serif;background:#15191f;color:#eee;padding:24px}'
        'td,th{padding:12px;border-bottom:1px solid #52606d;text-align:left}table{border-collapse:collapse}</style>'
        '<h1>虫棍M1：原始事件试听候选</h1><p>未确认招式名；数字是Wwise Event/MediaID，不是动画号。'
        '随机容器只取默认路径，Media列含候选分支，不表示全部同时播放。保留wwiser事件增益，'
        '未做额外响度归一化。E1需确认音色和用途。</p>'
        '<table><tr><th>来源/候选</th><th>身份</th><th>音频</th><th>播放</th></tr>' + ''.join(rows) + '</table>',
        encoding="utf-8")


def verify(work: Path) -> dict:
    banks = json.loads((work / "Inventory/banks.json").read_text(encoding="utf-8"))
    previews = json.loads((work / "Inventory/previews.json").read_text(encoding="utf-8"))
    exported = json.loads((work / "Inventory/exported_media.json").read_text(encoding="utf-8"))
    if len(banks) != len(DEFAULT_BANKS) or not exported:
        raise ValueError("incomplete bank/media evidence")
    for bank in banks:
        if sha256(Path(bank["source"])) != bank["sha256"] or sha256(Path(bank["copy"])) != bank["sha256"]:
            raise ValueError("bank source/copy hash changed")
    groups = Counter(item["bank"] for item in previews)
    if any(not groups[group] for group in GROUPS):
        raise ValueError("missing required audition group")
    for item in previews:
        metrics = wav_metrics(Path(item["wav"]))
        if metrics["sha256"] != item["sha256"] or not metrics["non_silent"]:
            raise ValueError("changed or silent preview")
    for item in exported:
        original = Path(item["source"]).read_bytes()
        payload = original[item["offset"]:item["offset"] + item["size"]]
        if hashlib.sha256(payload).hexdigest() != item["wem_sha256"] or sha256(Path(item["wem"])) != item["wem_sha256"]:
            raise ValueError("WEM does not match source-bank slice")
    result = {"success": True, "source_unchanged_banks": len(banks), "verified_wavs": len(previews),
              "verified_wems": len(exported), "by_group": dict(groups), "ue_imported": False,
              "listening_accepted": False}
    write_json(work / "Inventory/verification.json", result)
    return result


def publish(work: Path) -> Path:
    result = verify(work)
    banks = json.loads((work / "Inventory/banks.json").read_text(encoding="utf-8"))
    previews = json.loads((work / "Inventory/previews.json").read_text(encoding="utf-8"))
    nodes = json.loads((work / "Inventory/hirc.json").read_text(encoding="utf-8"))["nodes"]
    bank_by_path = {Path(bank["source"]).as_posix(): bank for bank in banks}
    metadata = {"stage": "M1", "date": "2026-10-17", "baseline": "af74c71",
                "game_version": "unknown; local unpack snapshot, not inferred from filenames",
                "tools": {"wwiser": {"tag": "v20260808", "commit": "94580dd3de3280933d69c435e96d4e2c3b443207",
                                      "url": "https://github.com/bnnm/wwiser/tree/v20260808"},
                          "vgmstream": {"version": "r1980 (Dec 31 2024)",
                                        "url": "https://raw.githubusercontent.com/bnnm/vgmstream-builds/master/bin/vgmstream-latest-test-u.zip",
                                        "zip_sha256": sha256(work / "Tools/vgmstream-maintainer.zip"),
                                        "exe_sha256": sha256(work / "Tools/vgmstream-maintainer/vgmstream-cli.exe")}},
                "banks": [{key: bank[key] for key in ("source", "sha256", "bytes", "version", "bank_id")}
                          for bank in banks],
                "events": [{key: node[key] for key in ("bank", "id", "name", "references")}
                           for node in nodes if node["type"] == "CAkEvent"],
                "previews": [], "verification": result}
    for item in previews:
        row = {key: item[key] for key in ("bank", "event_id", "media_ids", "conditions", "seconds",
                                         "sample_rate", "channels", "peak", "rms_dbfs", "sha256")}
        row.update({key: item[key] for key in ('gamesync_conditions', 'state_conditions', 'condition_sources') if key in item})
        row.update({"wav": Path(item["wav"]).relative_to(PROJECT).as_posix(),
                    "candidate_txtp": Path(item["file"]).name, "semantic_status": "await E1; not assigned to a GA",
                    "project_ga": None, "ue_sound": None, "notify_time": None,
                    "media_sources": {mid: [{"bank_id": bank_by_path[e["source"]]["bank_id"],
                                              "source": e["source"], "offset": e["offset"], "size": e["size"]}
                                             for e in entries if e["copied"]]
                                      for mid, entries in item["media_locations"].items()}})
        metadata["previews"].append(row)
    output = PROJECT / "docs/reference/虫棍音效M1资源清单.json"
    write_json(output, metadata)
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", choices=("prepare", "analyze", "audition", "verify", "publish"))
    parser.add_argument("--source-root", type=Path, default=Path("D:/study/MHR/natives/STM"))
    parser.add_argument("--work-root", type=Path, default=PROJECT / "Saved/Audio/RiseIG")
    parser.add_argument("--parse", action="store_true", help="run installed wwiser; analyze only")
    parser.add_argument("--per-group", type=int, default=4, choices=range(1, 9))
    args = parser.parse_args()
    work = args.work_root.resolve()
    if not work.is_relative_to(PROJECT / "Saved"):
        parser.error("work-root must stay inside project/Saved")
    if work == args.source_root.resolve() or args.source_root.resolve().is_relative_to(work):
        parser.error("source must not be inside output")
    if args.stage == "prepare":
        rows = prepare(args.source_root.resolve(), work)
        locations = source_media_index(args.source_root.resolve(), work)
        print(json.dumps({"banks": len(rows), "media": sum(len(row["media"]) for row in rows),
                          "indexed_media_ids": len(locations)}))
    elif args.stage == "analyze":
        if args.parse:
            parse_banks(work, work / "Tools/wwiser-v20260808/wwiser.py")
        summary = audit_events(work)
        write_json(work / "Inventory/summary.json", summary)
        print(json.dumps(summary))
    elif args.stage == "audition":
        previews = audition(work, work / "Tools/vgmstream-maintainer/vgmstream-cli.exe", args.per_group)
        page(work, previews)
        print(json.dumps({"previews": len(previews), "index": str(work / "Audition/index.html")}))
    elif args.stage == "verify":
        print(json.dumps(verify(work)))
    else:
        print(publish(work))


if __name__ == "__main__":
    main()
