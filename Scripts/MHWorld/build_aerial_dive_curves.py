"""Export observed LMT380 curves without averaging different planar inertias together.

No game files, UE assets, canonical truth tables, or recorder are changed.
Only generated analysis CSV/JSON/Markdown under the chosen Saved directory are written.
"""
import argparse
import csv
import json
from pathlib import Path

import numpy as np


def read(path):
    with path.open(encoding='utf-8-sig',newline='') as handle:
        return list(csv.DictReader(handle))


def write_csv(path,rows):
    with path.open('w',encoding='utf-8-sig',newline='') as handle:
        w=csv.DictWriter(handle,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)


def main():
    p=argparse.ArgumentParser();p.add_argument('directory',type=Path)
    args=p.parse_args();root=args.directory
    audits=read(root/'attempts.csv');mechanics=read(root/'mechanics_audit.csv')
    groups={}
    for row in read(root/'phase380_trials.csv'):
        groups.setdefault(int(row['attempt']),[]).append(row)
    clean=[];quality=[];switches=[]
    for a,m in zip(audits,mechanics):
        k=int(a['attempt']);rows=groups[k]
        d={key:np.array([float(r[key]) for r in rows]) for key in rows[0] if key!='quality'}
        t=d['time_s'];xz=np.column_stack([d['world_x'],d['world_z']]);xz-=xz[0]
        v=np.array([float(m['dive_vx']),float(m['dive_vz'])])
        h=xz@v/(v@v)
        cross=np.linalg.norm(xz-h[:,None]*v,axis=1)
        early=(t>=.05)&(t<=.55);late=(t>=.72)&(t<=min(.85,t[-1]-.03))
        ycoef=np.polyfit(t[late],d['up'][late],2)
        hcoef=np.polyfit(t[late],h[late],1)
        y_res=np.abs(d['up']-np.polyval(ycoef,t))
        h_res=np.abs(h-np.polyval(hcoef,t))*np.linalg.norm(v)
        usable=(float(m['early_horizontal_rms'])<5 and float(m['late_horizontal_rms'])<10
                and np.max(y_res[late])<3)
        bad=np.flatnonzero((t>=.85)&((y_res>10)|(h_res>3)|(cross>3)))
        cutoff=int(bad[0]) if len(bad) else len(t)
        # A boundary derivative belongs to the interval before its row, not the next action.
        accepted=np.arange(min(cutoff,len(t)))
        if usable:
            clean.append({'attempt':k,'t':t[accepted],'h':h[accepted],'up':d['up'][accepted],
                          'vy':d['vy'][accepted],'factor':(np.column_stack([d['vx'],d['vz']])@v/(v@v))[accepted]})
        jump=np.flatnonzero(d['gravity'][1:]!=d['gravity'][:-1])+1
        i=int(jump[0]) if len(jump) else -1
        if i>=0:
            switches.append({'attempt':k,'time_s':t[i],'native_frame':d['action_frame'][i],
                             'up':d['up'][i],'vy_before':d['vy'][i],
                             'first_dive_vy':d['vy'][i+1] if i+1<len(t) else None})
        quality.append({'attempt':k,'fit_usable':usable,'usable_prefix_samples':len(accepted),
                        'tail_deviation_start_s':float(t[cutoff]) if cutoff<len(t) else '',
                        'tail_deviation_start_sample':int(d['sample_index'][cutoff]) if cutoff<len(t) else '',
                        'max_cross_track_units':float(np.max(cross)),
                        'reason':'late_fit_window_nonuniform' if not usable else
                                 ('late_motion_deviation_excluded' if cutoff<len(t) else 'uniform_until_boundary')})
    assert len(clean)>=20,'Insufficient reliable trials'
    pooled=[]
    for tm in np.arange(0,1.175+1e-8,1/120):
        active=[d for d in clean if tm<=d['t'][-1]]
        if len(active)<5:break
        row={'time_s':round(float(tm),7),'contributors':len(active)}
        for key,out in [('h','horizontal_time_factor_s'),('up','up_units'),('vy','vertical_velocity_units_s'),('factor','horizontal_velocity_factor')]:
            values=np.array([np.interp(tm,d['t'],d[key]) for d in active])
            row[out]=float(np.median(values)) if tm>0 or key not in ['vy','factor'] else ''
            row[out+'_p05']=float(np.quantile(values,.05)) if row[out]!='' else ''
            row[out+'_p95']=float(np.quantile(values,.95)) if row[out]!='' else ''
        pooled.append(row)
    write_csv(root/'LMT380_pooled_parameter_curve.csv',pooled)
    reference=[]
    ref_m=mechanics[0]
    direction=np.array([float(ref_m['dive_vx']),float(ref_m['dive_vz'])]);direction/=np.linalg.norm(direction)
    refrows=groups[1];refq=quality[0]
    origin=np.array([float(refrows[0]['world_x']),float(refrows[0]['world_z'])])
    for i,r in enumerate(refrows[:int(refq['usable_prefix_samples'])]):
        delta=np.array([float(r['world_x']),float(r['world_z'])])-origin
        reference.append({'time_s':r['time_s'],'native_action_frame':r['action_frame'],
                          'horizontal_distance_units':float(delta@direction),'up_units':r['up'],
                          'horizontal_speed_units_s':r['speed_xz'] if i else '',
                          'vertical_velocity_units_s':r['vy'] if i else '',
                          'gravity_field':r['gravity'],'fall_speed_field':r['fall_speed'],
                          'source_sample_index':r['sample_index'],'attempt':1})
    write_csv(root/'LMT380_reference_attempt01_curve.csv',reference)
    write_csv(root/'trajectory_quality.csv',quality)
    write_csv(root/'stage_switches.csv',switches)
    assert len(switches)==len(audits),'Missing stage parameter switch'
    assert all(abs(float(m['speed_ratio'])-5/3)<.001 for m in mechanics),'Unexpected planar multiplier'
    assert all(abs(float(m['max_up'])-111.15)<.02 for m in mechanics),'Unexpected vertical apex'
    assert all(float(r['time_s'])<float(refq['tail_deviation_start_s']) for r in reference) if refq['tail_deviation_start_s']!='' else True
    assert all(pooled[i]['time_s']>pooled[i-1]['time_s'] for i in range(1,len(pooled)))
    def st(values):
        z=np.array(values,dtype=float)
        return {'min':float(np.min(z)),'median':float(np.median(z)),'max':float(np.max(z))}
    result={'trials':len(audits),'curve_trials':len(clean),'tail_deviation_trials':[q['attempt'] for q in quality if q['tail_deviation_start_s']!=''],
            'excluded_fit_trials':[q['attempt'] for q in quality if not q['fit_usable']],
            'switch_time_s':st([r['time_s'] for r in switches]),'switch_native_frame':st([r['native_frame'] for r in switches]),
            'apex_up_units':st([float(m['max_up']) for m in mechanics]),
            'entry_vertical_velocity_units_s':st([float(a['entry_vy']) for a in audits]),
            'entry_planar_speed_units_s':st([float(a['entry_speed_xz']) for a in audits]),
            'speed_ratio':st([float(m['speed_ratio']) for m in mechanics]),
            'vertical_early_gravity_observed_units_s2':st([float(m['early_gravity']) for m in mechanics]),
            'vertical_late_gravity_observed_units_s2':st([float(m['late_gravity']) for m in mechanics if quality[int(m['attempt'])-1]['fit_usable']]),
            'notes':['All units remain recorded MHW world units; not automatically multiplied by 100.',
                     'World up is +Y. Horizontals are XZ. UE world up is +Z: conversion is separate.',
                     'Sample at phase entry contains a derivative from the previous action; curve first velocity left blank.',
                     'Fit usability is numerical uniformity, not proof of no monster contact or wall hit.',
                     'Digital stick flags do not measure analog magnitude, camera yaw, or native input-latch time.',
                     'Inertia+300 directional addition is a hypothesis; continuing planar velocity ratios are observed.',
                     'Pooled h is seconds, multiplied by a fixed planar dive velocity VECTOR; never treat it as fixed horizontal distance.',
                     'Landing may end 380 before animation frame_end69; pooled endpoint is contributor-dependent, not fixed duration.']}
    (root/'curve_findings.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False))
    print('FILES',json.dumps({'reference_rows':len(reference),'pooled_rows':len(pooled),'validated':True}))


if __name__=='__main__':main()
