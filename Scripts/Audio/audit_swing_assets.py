"""Read-only UE snapshot for swing mapping; no Content setters/saves/compile.

Open editor: ue_remote.py this script. Closed: Python commandlet.
Reads only IG Montage/GA packages, their referenced Sequences and DA_IG_Combo.
Reuses the verified native exporter/parser and import-filename identity reader.
"""
from pathlib import Path
import hashlib
import json
import re
import sys
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/Audio/RiseIG/SwingMap'
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT / 'Scripts'))
sys.path.insert(0, str(ROOT / 'Scripts/AssetProbe'))
from ue_asset_utils import blueprint_cdo, decode_native_export, export_fields, export_records, read_field, split_export_fields
from read_asset_import_ids import RELATIVE

protected = {}
sequences = {}


def disk(path):
    package = path.split('.')[0]
    if not package.startswith('/Game/'):
        raise ValueError('Unexpected package path: ' + path)
    return ROOT / ('Content/' + package.removeprefix('/Game/') + '.uasset')


def protect(path):
    filename = disk(path)
    protected[filename.as_posix()] = hashlib.sha256(filename.read_bytes()).hexdigest()


def text_export(asset):
    filename = OUT / 'exports' / (asset.get_path_name().replace('/Game/', '').replace('/', '__') + '.copy')
    filename.parent.mkdir(parents=True, exist_ok=True)
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = str(filename)
    task.exporter = unreal.ObjectExporterT3D()
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    if not unreal.Exporter.run_asset_export_task(task):
        raise RuntimeError('Native export failed: ' + asset.get_path_name())
    return decode_native_export(filename.read_bytes())


def object_path(value):
    match = re.search(r"'(/Game/[^']+)'", value or '')
    return match[1] if match else None


def sequence(path):
    if path in sequences:
        return
    protect(path)
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError('Missing sequence: ' + path)
    model = asset.data_model_interface
    rate = model.get_frame_rate()
    fps = float(rate.numerator) / float(rate.denominator)
    if fps <= 0:
        raise ValueError('Invalid sequence frame rate: ' + path)
    sources = sorted({m.decode('utf-8') for m in RELATIVE.findall(disk(path).read_bytes())})
    # These editor-only properties are not exposed to Python reflection in 5.6.
    # Read their native export names instead of guessing Python spellings.
    native = text_export(asset)
    import_rates = {}
    for field in ('ImportFileFramerate', 'ImportResampleFramerate'):
        match = re.search(r'^\s*' + field + r'=([0-9.]+)\s*$', native, re.M)
        import_rates[field] = float(match[1]) if match else None
    sequences[path] = {'path': path, 'length_s': float(asset.get_play_length()),
                       'rate_scale': float(read_field(asset, 'rate_scale')),
                       'fps': fps, 'frames': int(model.get_number_of_frames()),
                       'import_file_fps': import_rates['ImportFileFramerate'],
                       'import_resample_fps': import_rates['ImportResampleFramerate'],
                       'import_sources': sources,
                       'import_numeric_ids': sorted({int(Path(name).stem) for name in sources if Path(name).stem.isdigit()})}


def montage(path):
    protect(path)
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError('Missing Montage: ' + path)
    text = text_export(asset)
    segments = []
    for slot_index, slot in enumerate(export_records(text, 'SlotAnimTracks')):
        track = export_fields(slot['AnimTrack'])
        for index, raw in enumerate(split_export_fields(track['AnimSegments'][1:-1])):
            row = export_fields(raw)
            source = object_path(row.get('AnimReference'))
            if source is None:
                raise ValueError('No source sequence in segment: ' + path)
            sequence(source)
            effective = float(row.get('AnimPlayRate', 1)) * sequences[source]['rate_scale']
            start, end = float(row.get('AnimStartTime', 0)), float(row['AnimEndTime'])
            segments.append({'slot': slot_index, 'index': index, 'sequence': source,
                             'source_start_s': start, 'source_end_s': end,
                             'track_start_s': float(row.get('StartPos', 0)),
                             'effective_rate': effective, 'loops': int(row.get('LoopingCount', 1)),
                             'raw': row})
    sections = [{'name': row['SectionName'].strip('"'),
                 'next': row.get('NextSectionName', '').strip('"'),
                 'time_s': float(row.get('LinkValue', 0)), 'raw': row}
                for row in export_records(text, 'CompositeSections')]
    return {'path': asset.get_path_name(), 'length_s': float(asset.get_play_length()),
            'rate_scale': float(read_field(asset, 'rate_scale')), 'segments': segments,
            'sections': sections, 'existing_notifies': export_records(text, 'Notifies')}


def ability(path):
    protect(path)
    asset = unreal.load_asset(path)
    cdo = blueprint_cdo(asset, unreal)
    text = text_export(cdo)
    # Native CDO text includes inherited/secondary Montage references without
    # guessing every subclass's property names or changing Blueprint state.
    refs = sorted(set(re.findall(r"/Game/Weapons/InsectGlaive/Anims/Montage/[A-Za-z0-9_]+\.[A-Za-z0-9_]+", text)))
    primary = re.search(r'^\s*AttackMontage=(.+)$', text, re.M)
    config = {}
    for name in ('default_entry_section', 'entry_section_by_transition_id', 'entry_section_by_source_state'):
        try:
            value = read_field(cdo, name)
            config[name] = {str(k): str(v) for k, v in value.items()} if hasattr(value, 'items') else str(value)
        except Exception as error:
            config[name] = {'read_error': str(error)}
    return {'path': asset.get_path_name(), 'class': cdo.get_class().get_path_name(),
            'montages': refs, 'primary_montage': object_path(primary[1]) if primary else None,
            'entry_config': config}


def main():
    report = {'schema': 'mhgz_swing_asset_snapshot_v1', 'success': False,
              'montages': [], 'abilities': [], 'sequences': sequences, 'errors': []}
    for folder, callback, target in (
        ('Content/Weapons/InsectGlaive/Anims/Montage', montage, 'montages'),
        ('Content/Blueprints/Ability/InsectGlaive', ability, 'abilities')):
        for filename in sorted((ROOT / folder).glob('*.uasset')):
            path = '/Game/' + filename.relative_to(ROOT / 'Content').with_suffix('').as_posix()
            try:
                report[target].append(callback(path))
            except Exception as error:
                report['errors'].append({'path': path, 'error': repr(error)})
    combo_path = '/Game/Weapons/InsectGlaive/Data/DA_IG_Combo'
    try:
        protect(combo_path)
        combo = unreal.load_asset(combo_path)
        report['combo_transitions'] = []
        for edge in read_field(combo, 'transitions'):
            ability_class = read_field(edge, 'ability_class')
            report['combo_transitions'].append({
                'transition_id': str(read_field(edge, 'transition_id')),
                'source_state': str(read_field(edge, 'source_state')),
                'target_state': str(read_field(edge, 'target_state')),
                'ability_class': ability_class.get_path_name() if ability_class else None})
    except Exception as error:
        report['errors'].append({'path': combo_path, 'error': repr(error)})
    unchanged = all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == fingerprint
                    for path, fingerprint in protected.items())
    report.update({'protected_package_hashes': protected, 'content_unchanged': unchanged,
                   'success': not report['errors'] and unchanged})
    (OUT / 'asset-snapshot.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('SWING_ASSET_SNAPSHOT ' + json.dumps({'success': report['success'],
          'montages': len(report['montages']), 'abilities': len(report['abilities']),
          'sequences': len(sequences), 'errors': report['errors']}, ensure_ascii=True))
    if not report['success']:
        raise RuntimeError('Read-only swing snapshot incomplete; inspect Saved report')


if __name__ == '__main__':
    main()
