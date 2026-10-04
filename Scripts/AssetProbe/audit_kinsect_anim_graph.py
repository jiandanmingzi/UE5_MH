"""Read-only dump of ABP_MH_Character AnimGraph node/pin wiring.

Run with UnrealEditor-Cmd.exe -run=pythonscript -script=Scripts/AssetProbe/audit_kinsect_anim_graph.py.
The report is written under Saved/AssetProbe; the Blueprint is never modified or saved.
"""

import json
from pathlib import Path

import unreal


ASSET_PATH = "/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character"
REPORT_PATH = Path(unreal.Paths.project_dir()) / "Saved" / "AssetProbe" / "kinsect_anim_graph.json"


def prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def object_name(obj):
    if obj is None:
        return None
    try:
        return obj.get_name()
    except Exception:
        return str(obj)


def graph_record(graph):
    member_names = [name for name in dir(graph) if "node" in name.lower()]
    raw_nodes = prop(graph, "nodes", None)
    node_collection = list(raw_nodes) if raw_nodes is not None else []
    if not node_collection:
        graph_nodes = getattr(graph, "get_graph_nodes_of_class", None)
        if callable(graph_nodes):
            for class_name in ("EdGraphNode", "AnimGraphNode_Base"):
                node_class = getattr(unreal, class_name, None)
                if node_class is None:
                    continue
                try:
                    node_collection = list(graph_nodes(node_class))
                    if node_collection:
                        break
                except Exception as exc:
                    member_names.append("{}: {}".format(class_name, exc))
    if not node_collection:
        for method_name in ("get_nodes", "get_graph_nodes"):
            method = getattr(graph, method_name, None)
            if callable(method):
                try:
                    node_collection = list(method())
                    if node_collection:
                        break
                except Exception:
                    pass
    nodes = []
    for node in node_collection:
        pins = []
        node_members = [name for name in dir(node) if any(
            token in name.lower() for token in ("pin", "title", "comment", "pos"))]
        raw_pins = prop(node, "pins", None)
        if raw_pins is None:
            get_pins = getattr(node, "get_pins", None)
            if callable(get_pins):
                try:
                    raw_pins = get_pins()
                except Exception as exc:
                    node_members.append("get_pins: {}".format(exc))
        for pin in raw_pins or []:
            links = []
            for linked in prop(pin, "linked_to", []) or []:
                owner = prop(linked, "owning_node")
                links.append({
                    "node": object_name(owner),
                    "node_class": owner.get_class().get_name() if owner else None,
                    "pin": str(prop(linked, "pin_name", "")),
                })
            pins.append({
                "name": str(prop(pin, "pin_name", "")),
                "direction": str(prop(pin, "direction", "")),
                "category": str(prop(prop(pin, "pin_type"), "category", "")),
                "links": links,
            })
        title = ""
        get_title = getattr(node, "get_node_title", None)
        if callable(get_title):
            try:
                title = str(get_title(unreal.NodeTitleType.LIST_VIEW))
            except Exception:
                pass
        nodes.append({
            "name": object_name(node),
            "class": node.get_class().get_name(),
            "members": node_members,
            "comment": str(prop(node, "node_comment", "")),
            "title": title,
            "x": prop(node, "node_pos_x"),
            "y": prop(node, "node_pos_y"),
            "pins": pins,
        })
    return {"name": object_name(graph), "class": graph.get_class().get_name(),
            "members": member_names, "reported_node_count": len(node_collection), "nodes": nodes}


asset = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
if asset is None:
    raise RuntimeError("Could not load {}".format(ASSET_PATH))

graphs = []
seen = set()
collections = {}
try:
    animation_graphs = asset.get_animation_graphs()
except Exception:
    animation_graphs = []
collections["get_animation_graphs()"] = [object_name(graph) for graph in animation_graphs]
for graph in animation_graphs:
    key = object_name(graph)
    if key not in seen:
        seen.add(key)
        graphs.append(graph_record(graph))

for collection_name in ("function_graphs", "ubergraph_pages", "macro_graphs",
                       "anim_graphs", "graphs", "all_graphs"):
    collection = prop(asset, collection_name, []) or []
    collections[collection_name] = [object_name(graph) for graph in collection]
    for graph in collection:
        key = object_name(graph)
        if key not in seen:
            seen.add(key)
            graphs.append(graph_record(graph))

REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
REPORT_PATH.write_text(json.dumps({
    "asset": ASSET_PATH,
    "asset_class": asset.get_class().get_name(),
    "asset_graph_members": [name for name in dir(asset) if "graph" in name.lower()],
    "unreal_node_types": [name for name in dir(unreal) if "graphnode" in name.lower()],
    "graph_collections": collections,
    "graph_count": len(graphs),
    "graphs": graphs,
}, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.log("[KinsectAnimGraphAudit] wrote {} graphs to {}".format(len(graphs), REPORT_PATH))
