"""Scoped 2026-10-15 locomotion boundary transaction; default is read-only verify.

UE Python Commandlet. Add -MHGZEntryRepairApply exactly once to author four
existing loops and the two dedicated entrance schemas. No ABP/ordinary Move,
Start, Stop, Montage or gameplay asset writes. Never auto-resume a partial save.
Uses the existing editor seam bridge, no Python controller reimplementation.
"""
import hashlib
import json
import math
import shutil
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir())
OUT=ROOT/'Saved/Maintenance/LocomotionEntryRepair20261015'
MM='/Game/Blueprints/Characters/Demo/Animation/MotionMatching/'
LOOPS=[
 '/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Loop',
 '/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Sprint_Loop_125x',
 '/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Walk_Loop',
 '/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Walk_Loop']
SCHEMAS=[MM+'PSS_MH_Shth_LoopPhase',MM+'PSS_MH_UnSh_LoopEntry']
PROTECTED=[MM+p for p in ('PSD_MH_Shth_Move','PSD_MH_UnSh_Move','PSS_MH_Move')]+[
 '/Game/Blueprints/Characters/Demo/Animation/ABP_MH_Character',
 '/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Run_Start',
 '/Game/Characters/Demo/Anims/Sequences/Unarmed/Locomotion/AS_Shth_Walk_Start',
 '/Game/Weapons/InsectGlaive/Anims/Sequences/Locomotion/AS_UnSh_Walk_Start']
TARGETS=LOOPS+SCHEMAS
APPLY='-MHGZEntryRepairApply' in unreal.SystemLibrary.get_command_line().split()
INTENT=OUT/'asset-intent.json'
DONE=OUT/'asset-applied.json'

def disk(path): return ROOT/('Content/'+path.removeprefix('/Game/')+'.uasset')
def sha(path): return hashlib.sha256(disk(path).read_bytes()).hexdigest()
def write(path,value): path.write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')
def vec(v): return [v.x,v.y,v.z]
def distance(a,b): return math.sqrt(sum((x-y)**2 for x,y in zip(a,b)))

def snapshot(path):
    sequence=unreal.load_asset(path)
    assert sequence
    model=sequence.data_model_interface
    count=model.get_number_of_frames()
    fps=model.get_frame_rate().numerator/model.get_frame_rate().denominator
    bones=model.get_bone_track_names()
    frames=[0,1,count//2,count-1,count]
    values=[]
    for frame in frames:
        transforms=unreal.AnimationLibrary.get_bone_poses_for_time(sequence,bones,min(frame/fps,sequence.get_play_length()),False)
        values.append({str(b):{'p':vec(t.translation),'r':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w],'s':vec(t.scale3d)} for b,t in zip(bones,transforms)})
    option=unreal.AnimPoseEvaluationOptions()
    curves=[]
    for frame in range(count+1):
        pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence,min(frame/fps,sequence.get_play_length()),option)
        names=unreal.AnimPoseExtensions.get_curve_names(pose)
        curves.append({str(n):unreal.AnimPoseExtensions.get_curve_weight(pose,n) for n in names})
    # Exported event values are native data, not guessed Python notify fields.
    notifies=[str(n) for n in unreal.AnimationLibrary.get_animation_notify_events(sequence)]
    return {'length':sequence.get_play_length(),'frames':count,'fps':fps,
            'bones':[str(b) for b in bones], 'values':values,'curves':curves,'notifies':notifies,
            'rate_scale':sequence.get_editor_property('rate_scale'),
            'root_lock':str(sequence.get_editor_property('root_motion_root_lock')),
            'force_root_lock':sequence.get_editor_property('force_root_lock')}

def component_boundary(path):
    seq=unreal.load_asset(path)
    length=seq.get_play_length()
    rows=[]
    for time in (0.,.001,length-.001,length):
        pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(seq,time,unreal.AnimPoseEvaluationOptions())
        rows.append({bone:vec(unreal.AnimPoseExtensions.get_bone_pose(pose,unreal.Name(bone),unreal.AnimPoseSpaces.WORLD).translation)
                     for bone in ('root','R_Foot_IK_end','L_Foot_IK_end')})
    def subtract(a,b): return [x-y for x,y in zip(a,b)]
    def feet(row): return [x for bone in ('R_Foot_IK_end','L_Foot_IK_end') for x in subtract(row[bone],row['root'])]
    first=[x/.001 for x in subtract(rows[1]['root'],rows[0]['root'])]
    last=[x/.001 for x in subtract(rows[3]['root'],rows[2]['root'])]
    return {'relative_feet_rms_cm':distance(feet(rows[0]),feet(rows[3]))/math.sqrt(2),
            'root_first_velocity_cm_s':first,'root_last_velocity_cm_s':last,
            'root_velocity_edge_difference_cm_s':distance(first,last)}

OUT.mkdir(parents=True,exist_ok=True)
if APPLY:
    assert not INTENT.exists() and not DONE.exists(),'Transaction exists; inspect saved postconditions, do not reapply'
    dirty={p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert not dirty.intersection(TARGETS),'Unsaved target edits; stop before backup or writes'
    protected={p:sha(p) for p in PROTECTED}
    before={p:snapshot(p) for p in LOOPS}
    backup=OUT/'AssetBackups'
    backup.mkdir(exist_ok=False)
    hashes={p:sha(p) for p in TARGETS}
    for path in TARGETS:
        target=backup/(path.rsplit('/',1)[-1]+'.uasset')
        shutil.copy2(disk(path),target)
        assert hashlib.sha256(target.read_bytes()).hexdigest()==hashes[path]
    write(INTENT,{'before':before,'before_sha256':hashes,'protected_sha256':protected,'backup':str(backup)})
    for path in LOOPS:
        assert unreal.MHGZAnimationSeamLibrary.close_locomotion_loop(unreal.load_asset(path),.1),path
    for i,path in enumerate(SCHEMAS):
        assert unreal.MHGZAnimationSeamLibrary.configure_loop_continuity_schema(unreal.load_asset(path),i==1),path
    # Check invariants in memory before the first formal asset save.
    after={p:snapshot(p) for p in LOOPS}
    for path in LOOPS:
        a,b=before[path],after[path]
        for key in ('length','frames','fps','bones','curves','notifies','rate_scale','root_lock','force_root_lock'):
            assert a[key]==b[key],(path,key)
        for bone in a['bones']:
            assert distance(a['values'][2][bone]['p'],b['values'][2][bone]['p'])<.001,(path,bone,'interior')
        for i in (0,4):
            assert distance(a['values'][i]['root']['p'],b['values'][i]['root']['p'])<.001,(path,'root distance')
    write(OUT/'memory-checked.json',after)
    for path in TARGETS:
        assert unreal.EditorAssetLibrary.save_loaded_asset(unreal.load_asset(path),only_if_is_dirty=False),path
    assert all(sha(p)==value for p,value in protected.items()),'Protected asset changed'
    write(DONE,{'after_sha256':{p:sha(p) for p in TARGETS},'backup':str(backup)})
else:
    assert DONE.exists(),'No applied checkpoint to verify'
    intent=json.loads(INTENT.read_text(encoding='utf-8'))
    done=json.loads(DONE.read_text(encoding='utf-8'))
    assert all(sha(p)==value for p,value in intent['protected_sha256'].items())
    assert all(sha(p)==value for p,value in done['after_sha256'].items())
    for path in LOOPS:
        actual=snapshot(path)
        expected=json.loads((OUT/'memory-checked.json').read_text(encoding='utf-8'))[path]
        assert actual==expected,(path,'disk reload differs')
    for i,path in enumerate(SCHEMAS):
        schema=unreal.load_asset(path)
        phase=[c for c in schema.get_editor_property('channels') if isinstance(c,unreal.MHGZPoseSearchFeatureChannel_LoopPhase)]
        assert len(phase)==1 and abs(phase[0].get_editor_property('entry_root_velocity_horizon')-.05)<1e-6
        assert phase[0].get_editor_property('bUnsheathed')==(i==1)
    boundaries={p:component_boundary(p) for p in LOOPS}
    assert all(v['relative_feet_rms_cm']<.1 for v in boundaries.values()),'Compressed component-space loop boundary did not close'
    write(OUT/'disk-verified.json',{'target_hashes_match':True,'protected_assets_unchanged':True,'reloaded_snapshots_match':True,
                                  'component_boundaries':boundaries})
unreal.log('[LocomotionEntryRepair] '+('scoped assets saved' if APPLY else 'independent disk reload verified'))
