"""Fail closed on missing trials or inconsistent frozen benchmark evidence."""
import csv
import json
from pathlib import Path
import sys
from analyze import summarize

ORDER = [False, True, True, False, True, False, False, True]
CASES = ['baseline', 'cpu_light', 'cpu_burst', 'cpu_sustained', 'dxc_mixed']

def verify(root):
    metadata = json.loads((root / 'metadata.json').read_text())
    assert metadata['order'] == ORDER
    paths = list(root.glob('*/result.json'))
    assert len(paths) == 40, f'expected 40 results, got {len(paths)}'
    jobs_total = samples_total = 0
    for case in CASES:
        for index, low in enumerate(ORDER):
            directory = root / (f'{case}-{index:02}-' + ('B' if low else 'A'))
            result = json.loads((directory / 'result.json').read_text())
            assert result['kind'] == case and result['low_priority'] == low
            assert result['duration_s'] == 45 and not result['errors']
            assert result['iterations'] == metadata['calibration']['iterations']
            assert 44 <= result['foreground']['wall_s'] <= 47
            assert result['foreground']['nice'] == 0
            assert result['instrument'] == (not low if case == 'baseline' else True)
            for state in [result['start_state'], result['end_state'], *result['telemetry']]:
                assert state['thermal'] < 2 and not state['low_power']
            rows = [{k: float(v) for k, v in row.items()}
                    for row in csv.DictReader((directory / 'samples.csv').open())]
            if result['instrument']:
                measured = summarize(rows, expected=result['foreground']['frames'])
                assert measured == result['metrics'], 'raw/recorded metric mismatch'
                assert rows[-1]['target_s'] < 45
                assert all(b['wake_s'] >= a['finish_s'] for a, b in zip(rows, rows[1:])), 'overlapping foreground work'
                samples_total += len(rows)
            else:
                assert not rows and result['metrics'] is None
            assert result['competitors'] == (3 if case in ['cpu_sustained', 'dxc_mixed'] else 0)
            for worker in result['burn_workers']:
                assert worker['nice'] == 5 * low and worker['chunks'] > 0
                assert 44 <= worker['wall_s'] <= 47
            if case.startswith('cpu_'):
                assert len(result['burn_workers']) == 2
            if case == 'dxc_mixed':
                jobs = result['jobs']
                ids = [job['id'] for job in jobs]
                assert len(ids) == len(set(ids))
                unlaunched = result['not_launched_at_end_ids']
                assert len(unlaunched) == len(set(unlaunched))
                assert not set(ids) & set(unlaunched)
                assert set(ids) | set(unlaunched) == set(range(result['jobs_reserved']))
                # end_s is sampled after releasing the launch mutex, so small
                # bookkeeping overlaps cannot establish subprocess concurrency.
                for job in jobs:
                    assert job['nice'] == 5 * low and job['validated']
                    assert job['returncode'] == job['system_status'] == 0
                    assert job['spirv_bytes'] > 20 and len(job['spirv_sha256']) == 64
                    assert 0 <= job['start_s'] < job['end_s'] and 0 < job['wall_s'] <= 15
                occupancy = sum(max(0, min(j['end_s'], 45) - max(j['start_s'], 0)) for j in jobs) / 45
                assert occupancy == result['compiler_occupancy'] and .8 <= occupancy <= 1
                assert result['dxc_launch_concurrency'] == 1
                assert result['compile_throughput_per_s'] == sum(j['end_s'] <= 45 for j in jobs) / 45
                jobs_total += len(jobs)
    return {'status': 'PASS', 'trials': 40, 'raw_samples': samples_total,
            'validated_DXC_jobs': jobs_total, 'protocol_commit': metadata['git_commit']}

if __name__ == '__main__':
    print(json.dumps(verify(Path(sys.argv[1])), indent=2))
