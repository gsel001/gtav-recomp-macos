"""Export trial-level and paired effects from the frozen benchmark raw samples."""
import argparse
import csv
import json
from pathlib import Path
import statistics
from analyze import summarize, percentile

PAIR_INDICES = [(0, 1), (3, 2), (5, 4), (6, 7)]  # A,B in ABBA/BAAB

def paired_effect(group, field):
    if len(group) != 8 or [r['B_low_priority'] for r in group] != [False, True, True, False, True, False, False, True]:
        raise ValueError('expected eight ordered ABBA/BAAB trials')
    deltas=[group[b][field]-group[a][field] for a,b in PAIR_INDICES]
    mean=statistics.mean(deltas); se=statistics.stdev(deltas)/2
    return {'mean_A':statistics.mean(r[field] for r in group if not r['B_low_priority']),
            'mean_B':statistics.mean(r[field] for r in group if r['B_low_priority']),
            'paired_B_minus_A':deltas,'mean_delta':mean,'range_delta':[min(deltas),max(deltas)],
            'approx_95pct_t_interval':[mean-3.182*se,mean+3.182*se]}

def export(root, out):
    out.mkdir(parents=True, exist_ok=True)
    rows=[]
    for p in sorted(root.glob('*/result.json')):
        r=json.loads(p.read_text())
        valid=not r['errors'] and r['duration_s']==45
        row={'trial':p.parent.name,'case':r['kind'],'B_low_priority':r['low_priority'],
             'instrument':r['instrument'],'valid':valid,'errors':'; '.join(r['errors']),
             'frames':r['foreground']['frames'],'skipped_slots':r['foreground']['skipped_slots'],
             'foreground_cpu_s':r['foreground']['cpu_s'],
             'foreground_peak_rss_MB':r['foreground']['peak_rss_bytes']/1e6,
             'thermal_max':max(x['thermal'] for x in r['telemetry']),
             'compile_throughput_per_s':r.get('compile_throughput_per_s'),
             'compiler_occupancy':r.get('compiler_occupancy'),
             'compile_wall_p95_s':r.get('compile_wall_p95_s'),
             'shader_jobs':len(r['jobs']),
             'validated_jobs':sum(j['validated'] for j in r['jobs']),
             'compiler_cpu_s':sum(j['cpu_s'] for j in r['jobs']),
             'compiler_peak_rss_MB':max((j['peak_rss_bytes']/1e6 for j in r['jobs']),default=0),
             'burn_chunks_per_s':sum(w['chunks'] for w in r['burn_workers'])/45,
             'burn_cpu_s':sum(w['cpu_s'] for w in r['burn_workers'])}
        if r['jobs']:
            row['jobs_completed_in_window']=sum(j['end_s']<=45 for j in r['jobs'])
            row['reserved_not_launched']=len(r['not_launched_at_end_ids'])
            row['drain_tail_s']=max(0,max(j['end_s'] for j in r['jobs'])-45)
            for label,values in [('job_wall_s',[j['wall_s'] for j in r['jobs']]),
                                 ('queue_wait_s',[j['queue_wait_s'] for j in r['jobs']])]:
                for pctl in (50,95,99): row[f'{label}_p{pctl}']=percentile(values,pctl)
                row[label+'_max']=max(values)
        if r['instrument']:
            raw=[{k:float(v) for k,v in x.items()} for x in csv.DictReader((p.parent/'samples.csv').open())]
            s=summarize(raw,expected=r['foreground']['frames'],failed=bool(r['errors']))
            row['deadline_overrun_gt_1ms']=s['completion_overrun_gt_1ms']
            for name in ('wake_lateness_ms','service_ms','response_ms','deadline_overrun_ms'):
                for metric,value in s[name].items(): row[name+'_'+metric]=value
        rows.append(row)
    keys=list(dict.fromkeys(k for r in rows for k in r))
    with (out/'trials.csv').open('w') as f:
        writer=csv.DictWriter(f,fieldnames=keys);writer.writeheader();writer.writerows(rows)
    pairs={}
    for case in ('cpu_light','cpu_burst','cpu_sustained','dxc_mixed'):
        group=[r for r in rows if r['case']==case]
        if len(group)!=8 or not all(r['valid'] for r in group):
            pairs[case]={'status':'INCOMPLETE/INVALID','trials':len(group)};continue
        fields=['response_ms_p99','wake_lateness_ms_p99','deadline_overrun_gt_1ms','skipped_slots']
        fields+=['compile_throughput_per_s','compile_wall_p95_s'] if case=='dxc_mixed' else ['burn_chunks_per_s']
        pairs[case]={}
        for field in fields:
            pairs[case][field]=paired_effect(group,field)
    summary={'trial_count':len(rows),'valid_trial_count':sum(r['valid'] for r in rows),
             'pairs':pairs,'note':'Four adjacent pairs/case; exploratory t interval assumes independent pair effects. Not GTA measurements.'}
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('out',type=Path)
    a=p.parse_args();export(a.root,a.out)
