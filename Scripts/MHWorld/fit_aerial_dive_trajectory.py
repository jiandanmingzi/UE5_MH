"""Fit observations, excluding phase-entry carryover and contact-distorted samples."""
import argparse
import csv
import json
from pathlib import Path

import numpy as np


def read(path):
    with path.open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def main():
    p=argparse.ArgumentParser()
    p.add_argument("directory",type=Path)
    args=p.parse_args()
    audits=read(args.directory/"attempts.csv")
    grouped={}
    for row in read(args.directory/"phase380_trials.csv"):
        grouped.setdefault(int(row["attempt"]),[]).append(row)
    stats=[]
    for a in audits:
        k=int(a["attempt"])
        rows=grouped[k]
        d={name:np.array([float(r[name]) for r in rows]) for name in rows[0] if name not in ("quality",)}
        t=d["time_s"]; f=d["action_frame"]
        early=(t>=.05)&(t<=.55)
        late=(t>=.72)&(t<=min(.85,t[-1]-.03))
        ev=np.array([np.median(d[v][early]) for v in ("vx","vz")])
        lv=np.array([np.median(d[v][late]) for v in ("vx","vz")])
        speed1=np.linalg.norm(ev);speed2=np.linalg.norm(lv)
        row={"attempt":k,"prev_id":a["prev_id"],"duration":t[-1],"first_vx":ev[0],"first_vz":ev[1],
             "first_speed":speed1,"dive_vx":lv[0],"dive_vz":lv[1],"dive_speed":speed2,
             "speed_ratio":speed2/speed1 if speed1>1e-4 else 0.,
             "rt_age":a["rt_age_s"],"rt_stick_x":a["rt_stick_x"],"rt_stick_y":a["rt_stick_y"],
             "entry_yaw":a["entry_yaw"],"stable_yaw":np.median(d["yaw_deg"][early]),
             "stick_on_fraction":a["stick_on_fraction"],
             "early_horizontal_rms":np.sqrt(np.mean((d["vx"][early]-ev[0])**2+(d["vz"][early]-ev[1])**2)),
             "late_horizontal_rms":np.sqrt(np.mean((d["vx"][late]-lv[0])**2+(d["vz"][late]-lv[1])**2)),
             "max_up":np.max(d["up"]),"gravity_values":";".join(map(str,np.unique(d["gravity"]))),
             "fall_min":np.min(d["fall_speed"]),"fall_max":np.max(d["fall_speed"])}
        # Positive magnitude is native downwards fall speed; verify rather than assuming.
        fit1=np.polyfit(t[early],d["up"][early],2)
        fit2=np.polyfit(t[late],d["up"][late],2)
        row.update(early_gravity=-2*fit1[0],early_v0=fit1[1],early_offset=fit1[2],
                   late_gravity=-2*fit2[0],late_v_intercept=fit2[1],late_offset=fit2[2])
        pre_yaw=np.rad2deg(np.arctan2(float(a['entry_vx']),float(a['entry_vz'])))
        old_yaw=float(a['entry_yaw'])
        offset=(pre_yaw-old_yaw+180)%360-180
        inertia_yaw=old_yaw+90*round(offset/90)
        inertia_dir=np.array([np.sin(np.deg2rad(inertia_yaw)),np.cos(np.deg2rad(inertia_yaw))])
        new_dir=np.array([np.sin(np.deg2rad(row['stable_yaw'])),np.cos(np.deg2rad(row['stable_yaw']))])
        inertia_speed=400 if float(a['entry_speed_xz'])<500 else 900
        candidates=[(0,inertia_speed*inertia_dir),(300,inertia_speed*inertia_dir+300*new_dir)]
        best=min(candidates,key=lambda c:np.linalg.norm(c[1]-lv))
        row.update(inertia_yaw=inertia_yaw,inferred_inertia_speed=inertia_speed,
                   inferred_directional_addition=best[0],vector_model_error=float(np.linalg.norm(best[1]-lv)))
        jump=np.flatnonzero((d["gravity"][1:]!=d["gravity"][:-1]))+1
        row["gravity_changes"]=";".join(f"{t[i]:.6f}/{f[i]:.4f}:{d['gravity'][i]:.2f}" for i in jump)
        stats.append(row)
    with (args.directory/"mechanics_audit.csv").open("w",encoding="utf-8-sig",newline="") as handle:
        writer=csv.DictWriter(handle,fieldnames=list(stats[0]));writer.writeheader();writer.writerows(stats)
    print("OVERVIEW",json.dumps({"attempts":len(stats),"speed_ratio_range":[min(s['speed_ratio'] for s in stats),max(s['speed_ratio'] for s in stats)]}))
    print("MODEL",json.dumps({"median_vector_error":float(np.median([s['vector_model_error'] for s in stats])),
                              "p95_vector_error":float(np.quantile([s['vector_model_error'] for s in stats],.95)),
                              "max_vector_error":max(s['vector_model_error'] for s in stats)}))


if __name__=="__main__":
    main()
