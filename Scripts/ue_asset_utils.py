"""Small, UE-independent adapters for reflected values; errors stay explicit."""

from collections.abc import Mapping
import re


def read_field(obj, name):
    return obj.get_editor_property(name)


def read_field_or_error(obj, name):
    try:
        return read_field(obj, name)
    except Exception as exc:
        return {"read_error": repr(exc)}


def is_read_error(value):
    return isinstance(value, dict) and "read_error" in value


def as_sequence(value):
    # Error dictionaries and strings are iterable too, but are never UE arrays.
    if isinstance(value, (str, bytes, Mapping)) or hasattr(value, "items"):
        raise TypeError(f"Expected reflected array, got {type(value).__name__}")
    try:
        return list(value)
    except TypeError as exc:
        raise TypeError(f"Expected reflected array, got {type(value).__name__}") from exc


def map_items(value):
    if is_read_error(value) or not hasattr(value, "items"):
        raise TypeError(f"Expected reflected map, got {type(value).__name__}")
    return list(value.items())


def map_values(value):
    return {str(key): item for key, item in map_items(value)}


def scalable_float_value(value):
    if is_read_error(value):
        return value
    return {"value": float(read_field(value, "value")), "curve": str(read_field(value, "curve"))}


def blueprint_cdo(asset, unreal):
    if asset is None or not callable(getattr(asset, "generated_class", None)):
        raise TypeError("Expected a loaded Blueprint asset")
    generated_class = asset.generated_class()
    if generated_class is None:
        raise RuntimeError("Blueprint has no generated class; check compilation")
    return unreal.get_default_object(generated_class)


def split_export_fields(text):
    """Existing native T3D parser, promoted from the verified Montage inventory probe."""
    depth, quoted, start, items = 0, False, 0, []
    for index, char in enumerate(text):
        if char == '"':
            quoted = not quoted
        if quoted:
            continue
        if char == '(':
            depth += 1
        elif char == ')':
            depth -= 1
        elif char == ',' and depth == 0:
            items.append(text[start:index])
            start = index + 1
    items.append(text[start:])
    return items


def export_fields(text):
    return dict(item.split('=', 1) for item in split_export_fields(text[1:-1]) if '=' in item)


def export_records(text, key):
    return [export_fields(match.group(1)) for match in
            re.finditer(r'(?m)^\s*' + re.escape(key) + r'\(\d+\)=(.*)$', text)]


def decode_native_export(data):
    """UE's text exporter switches to UTF-16 when an asset contains non-ASCII text."""
    text = data.decode('utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')
    return text.replace('\r\n', '\n').replace('\r', '\n')


def montage_removal_recipe(snapshot, removed_assets, section_aliases=None, clamp_accept_ends=False):
    """Remove explicit clips only; collapse authoring times without changing source cuts."""
    segments = snapshot['segments']
    removed = [clip for clip in segments if clip['asset'] in removed_assets]
    if {clip['asset'] for clip in removed} != set(removed_assets) or len(removed) != len(removed_assets):
        raise ValueError('Expected exactly one segment per explicit removal asset')
    intervals = [(clip['start'], clip['start'] + clip['duration']) for clip in removed]
    aliases = section_aliases or {}
    def retime(value):
        return max(0.0, value - sum(max(0.0, min(value, end) - start) for start, end in intervals))
    def boundary(value):
        if any(start + 2e-5 < value < end - 2e-5 for start, end in intervals):
            raise ValueError('Authored section/notify boundary lies inside a removed clip')
        return retime(value)
    kept = [{key: clip[key] for key in ('asset', 'source_start', 'source_end', 'play_rate', 'loops')}
            for clip in segments if clip['asset'] not in removed_assets]
    if not kept or any(clip['loops'] != 1 for clip in kept):
        raise ValueError('Recipe requires nonempty, single-loop clips')
    sections = []
    old_sections = {row['SectionName'].strip('"'): row for row in snapshot['sections']}
    for alias, target in aliases.items():
        if alias not in old_sections or target not in old_sections or target in aliases:
            raise ValueError('Invalid section alias')
        if abs(boundary(float(old_sections[alias].get('LinkValue', 0)))
               - boundary(float(old_sections[target].get('LinkValue', 0)))) > 2e-5:
            raise ValueError('Only collapsed, equivalent Section entries may be merged')
    for row in snapshot['sections']:
        if any('metadata' in key.lower() for key in row):
            raise ValueError('Section metadata requires a preserving native operation')
        name = row['SectionName'].strip('"')
        if name in aliases:
            continue
        next_name = row.get('NextSectionName', '').strip('"')
        sections.append({'name': name,
                         'next': aliases.get(next_name, next_name),
                         'time': boundary(float(row.get('LinkValue', 0)))})
    if any(right['time'] <= left['time'] for left, right in zip(sections, sections[1:])):
        raise ValueError('Removal collapses distinct Sections')
    notifies = []
    for index, event in enumerate(snapshot['notifies']):
        end = event['end']
        if clamp_accept_ends and end > snapshot['length'] + 2e-5:
            if event['row']['NotifyName'].strip('"') not in ('ComboWindow', 'DodgeAcceptWindow'):
                raise ValueError('Non-accept notify exceeds Montage; do not silently trim it')
            end = snapshot['length']
        notifies.append({'old_index': index, 'start': boundary(event['start']),
                         'end': boundary(end), 'clone': False})
    for old, new in zip(snapshot['notifies'], notifies):
        if old['end'] > old['start'] and new['end'] <= new['start']:
            raise ValueError('Removal collapses a notify state window')
    return {'segments': kept, 'sections': sections, 'notifies': notifies}
