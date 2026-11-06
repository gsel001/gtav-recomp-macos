"""Bounded Mac host/DXC contention matrix. Never launches GTA or changes its files."""
import argparse
import concurrent.futures
import csv
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import threading
import time
from analyze import summarize, percentile

SEED = 20261105
ORDER = [False, True, True, False, True, False, False, True]  # ABBA / BAAB
BASE = Path(__file__).resolve().parents[2]

def command(args, **kwargs):
    return subprocess.run(list(map(str, args)), capture_output=True, text=True, timeout=15, **kwargs)

def system_state(worker):
    r = command([worker, 'env']); r.check_returncode()
    return {**json.loads(r.stdout), 'power': command(['pmset', '-g', 'batt']).stdout.strip(),
            'swap': command(['sysctl', 'vm.swapusage']).stdout.strip(),
            'memory_pressure': command(['memory_pressure', '-Q']).stdout.strip()}

def idle_check(worker, wait=True):
    for _ in range(41 if wait else 1):
        r = command(['pgrep', '-x', 'gtav_fast'])
        if r.returncode != 1:
            raise RuntimeError('GTA active or process check failed: no heavy trials authorized in this state')
        state = system_state(worker)
        # NSProcessInfo: nominal=0, fair=1, serious=2, critical=3.
        if state['thermal'] == 0 and not state['low_power']:
            return state
        if not wait: break
        print('COOLDOWN: waiting for nominal thermal state / normal power', flush=True)
        time.sleep(15)
    raise RuntimeError('thermal/power conditions unsuitable; matrix paused')

def corpus(folder):
    folder.mkdir()
    for i in range(24):
        if i % 4 == 0:
            text = 'float4 main(float4 p:POSITION):SV_Position { float4 q=p;\n'
            for k in range(96 + i * 2):
                text += f'q=sin(q.wxyz*{1.001+k*.003:.6f}+p*{.01+k*.001:.6f});\n'
            text += 'return p+q*float(TRIAL_JOB%31+1)*0.00001;}\n'
        else:
            text = 'Texture2D<float4> tx:register(t0); SamplerState smp:register(s0);\nfloat4 main(float4 p:SV_Position):SV_Target { float2 uv=p.xy*.001; float4 q=0;\n'
            for k in range(128 + (i % 3)*96):
                text += f'q+=tx.SampleLevel(smp,uv+float2({(k+1)*.0003:.7f},{(k+1)*.0007:.7f}),0)*{1/(k+1):.8f};\n'
                if i % 3 == 2:
                    text += f'if(q.x>{k*.01:.5f}) q=sin(q)+q.yzwx*.1;\n'
            text += 'return q*float(TRIAL_JOB%31+1)*0.01;}\n'
        (folder / f'{i:02}.hlsl').write_text(text)
    return hashlib.sha256(b''.join(p.name.encode()+p.read_bytes() for p in sorted(folder.iterdir()))).hexdigest()

def stop_owned(p):
    if p.poll() is None:
        os.killpg(p.pid, signal.SIGTERM)
        try: p.wait(3)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL); p.wait()

def trial(worker, dxc, validator, root, kind, low, seconds, iterations, instrument=True):
    root.mkdir(); start_state = idle_check(worker)
    owned, lock, jobs, errors, telemetry = [], threading.Lock(), [], [], []
    launch_lock = threading.Lock()  # model observed same-parent Darwin system() serialization
    def spawn(args, name):
        log = (root / (name+'.stdout')).open('w')
        p = subprocess.Popen(list(map(str,args)), stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
                             env={**os.environ, 'DYLD_LIBRARY_PATH':str(dxc.parent/'lib')})
        log.close()
        with lock: owned.append(p)
        return p
    foreground = None
    end = time.monotonic()+seconds
    next_job = 0
    stop = threading.Event()
    competitors = 3 if kind in ('cpu_sustained','dxc_mixed') else 0
    duty = {'cpu_light':.1, 'cpu_burst':.5, 'cpu_sustained':1}.get(kind, 1)
    def compile_jobs():
        nonlocal next_job
        while not stop.is_set() and time.monotonic()<end:
            with lock: n=next_job; next_job+=1
            if n >= 4000:
                errors.append('job bound reached'); break
            meta, nice, output = (root / f'job-{n}.{suffix}' for suffix in ('json','nice','spv'))
            source = root.parent/'corpus'/f'{n%24:02}.hlsl'
            profile = 'vs_6_0' if n%24%4==0 else 'ps_6_0'
            args = [dxc,'-T',profile,'-HV','2021','-all-resources-bound','-spirv','-fvk-use-dx-layout',
                    '-Wno-ignored-attributes','-Qstrip_debug','-D',f'TRIAL_JOB={n}','-Fo',output,source]
            if profile=='vs_6_0': args.insert(1,'-fvk-invert-y')
            queued=time.monotonic()
            p=None
            try:
                with launch_lock:
                    if stop.is_set() or time.monotonic()>=end: return
                    p=spawn([worker,'launch',int(low),meta,worker,'exec',nice,*args],f'job-{n}')
                    begin=time.monotonic()
                    p.wait(15)
                record={'id':n,'source':source.name,'start_s':begin-trial_start,
                        'queue_wait_s':begin-queued,
                        'end_s':time.monotonic()-trial_start,'returncode':p.returncode,
                        **json.loads(meta.read_text()),**json.loads(nice.read_text())}
                record['validated']=False
                if p.returncode==0 and output.exists():
                    data=output.read_bytes()
                    record['spirv_bytes']=len(data)
                    record['spirv_sha256']=hashlib.sha256(data).hexdigest()
                    validation=command([validator,output])
                    record['validated']=len(data)>=20 and data[:4]==b'\x03\x02\x23\x07' and validation.returncode==0
                if not record['validated'] or record['nice']!=int(low)*5:
                    errors.append('compile/validation/priority failure at job '+str(n))
                with lock: jobs.append(record)
                output.unlink(missing_ok=True)
            except Exception as error:
                if p is not None: stop_owned(p)
                errors.append(str(error)); break
    trial_start = time.monotonic(); end=trial_start+seconds
    pool=concurrent.futures.ThreadPoolExecutor(max_workers=2)
    futures=[]
    try:
        if kind.startswith('cpu_'):
            for i in range(2):
                spawn([worker,'launch',int(low),root/f'burn-{i}.json',worker,'burn',seconds,str(duty),root/f'burn-{i}.nice'],f'burn-{i}')
        elif kind=='dxc_mixed':
            futures=[pool.submit(compile_jobs) for _ in range(2)]
        for i in range(competitors):
            spawn([worker,'burn',seconds,'1',root/f'competitor-{i}.nice'],f'competitor-{i}')
        foreground=spawn([worker,'foreground',seconds,iterations,int(instrument),root/'samples.csv','0'],'foreground')
        while foreground.poll() is None:
            if command(['pgrep','-x','gtav_fast']).returncode!=1:
                errors.append('GTA started or process visibility failed'); stop.set(); break
            env=json.loads(command([worker,'env']).stdout)
            if env['thermal']>=2:
                errors.append('serious/critical thermal condition'); stop.set(); break
            with lock: pids=[p.pid for p in owned if p.poll() is None]
            # Known owned launcher processes; per-job rusage also captures descendants.
            ps=command(['ps','-p',','.join(map(str,pids)),'-o','pid=,pcpu=,rss=']) if pids else None
            telemetry.append({'elapsed_s':time.monotonic()-trial_start,**env,'owned_ps':ps.stdout if ps else ''})
            time.sleep(1)
        stop.set()
        for future in futures: future.result(timeout=20)
        for p in owned:
            try: p.wait(8)
            except subprocess.TimeoutExpired: stop_owned(p); errors.append('owned child timeout')
        fg=json.loads((root/'foreground.stdout').read_text())
        rows=list(csv.DictReader((root/'samples.csv').open()))
        rows=[{k:float(v) for k,v in row.items()} for row in rows]
        summary=summarize(rows,expected=fg['frames']) if instrument else None
        workers=[]
        for i in range(2 if kind.startswith('cpu_') else 0):
            detail=json.loads((root/f'burn-{i}.nice.result').read_text())
            nice=json.loads((root/f'burn-{i}.nice').read_text())
            if nice['nice']!=int(low)*5: errors.append('burn priority mismatch')
            workers.append({**detail,**nice})
        result={'kind':kind,'low_priority':low,'duration_s':seconds,'iterations':iterations,'instrument':instrument,
                'competitors':competitors,'compiler_workers':2 if kind!='baseline' else 0,
                'foreground':fg,'metrics':summary,'jobs':jobs,'burn_workers':workers,
                'jobs_reserved':next_job,'not_launched_at_end_ids':sorted(set(range(next_job))-{j['id'] for j in jobs}),
                'start_state':start_state,'end_state':system_state(worker),'errors':errors,
                'telemetry':telemetry,'total_wall_s':time.monotonic()-trial_start}
        if kind=='dxc_mixed':
            active=sum(max(0,min(j['end_s'],seconds)-max(j['start_s'],0)) for j in jobs)/seconds
            result['compiler_occupancy']=active
            result['dxc_launch_concurrency']=1
            result['compile_throughput_per_s']=sum(j['end_s']<=seconds for j in jobs)/seconds
            result['compile_wall_p95_s']=percentile([j['wall_s'] for j in jobs],95) if jobs else None
            if active<.8: errors.append('insufficient sustained compiler activity (<80%)')
        (root/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        if errors: raise RuntimeError(str(errors))
        return result
    finally:
        stop.set()
        for p in owned: stop_owned(p)
        pool.shutdown(wait=True, cancel_futures=True)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--worker',type=Path,required=True); p.add_argument('--dxc',type=Path,required=True)
    p.add_argument('--validator',type=Path,required=True); p.add_argument('--out',type=Path,required=True)
    p.add_argument('--pilot',action='store_true'); a=p.parse_args()
    root=a.out.resolve(); root.mkdir(); worker=a.worker.resolve(); dxc=a.dxc.resolve(); validator=a.validator.resolve()
    state=idle_check(worker)
    calibration=json.loads(command([worker,'calibrate']).stdout)
    corpus_hash=corpus(root/'corpus')
    metadata={'seed':SEED,'order':ORDER,'calibration':calibration,'corpus_sha256':corpus_hash,
              'worker_sha256':hashlib.sha256(worker.read_bytes()).hexdigest(),
              'dxc_sha256':hashlib.sha256(dxc.read_bytes()).hexdigest(),
              'git_commit':command(['git','-C',BASE,'rev-parse','HEAD']).stdout.strip(),
              'dxc_version':command([dxc,'--version'],env={**os.environ,'DYLD_LIBRARY_PATH':str(dxc.parent/'lib')}).stdout,
              'validator_version':command([validator,'--version']).stdout,'machine':command(['sw_vers']).stdout,
              'initial_state':state,'protocol':'45 seconds; ABBA BAAB; 1ms completion-overrun tolerance; <=6 heavy workers; DXC two queues / one serialized child launch'}
    (root/'metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
    cases=['baseline','cpu_light','cpu_burst','cpu_sustained','dxc_mixed']
    for kind in cases:
        order=[False] if a.pilot else ORDER
        for i,low in enumerate(order):
            # Baseline toggles collection to estimate instrumentation cost; no compilers.
            instrument=(not low) if kind=='baseline' else True
            name=f'{kind}-{i:02}-'+('B' if low else 'A')
            print('START',name,flush=True)
            result=trial(worker,dxc,validator,root/name,kind,low,5 if a.pilot else 45,calibration['iterations'],instrument)
            print('DONE',name,'frames',result['foreground']['frames'],'errors',result['errors'],flush=True)
            time.sleep(3 if a.pilot else 10)
    print('MATRIX COMPLETE',root,flush=True)

if __name__=='__main__': main()
