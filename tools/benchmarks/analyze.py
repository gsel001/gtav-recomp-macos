"""Analyze raw host timing samples; never describes them as GTA/display FPS."""
import math

def percentile(values, p):
    if not values or not 0 < p <= 100:
        raise ValueError('empty sample or invalid percentile')
    return sorted(values)[math.ceil(len(values) * p / 100) - 1]

def summarize(rows, expected=None, failed=False):
    if failed:
        raise ValueError('failed trial must not enter valid performance summary')
    if not rows or (expected is not None and len(rows) != expected):
        raise ValueError('missing samples')
    last = -1.0
    for row in rows:
        t, w, f = (row[k] for k in ('target_s', 'wake_s', 'finish_s'))
        if not all(math.isfinite(v) for v in (t, w, f)) or t < 0 or t <= last or w < t or f < w:
            raise ValueError('invalid sample')
        last = t
    vectors = {
        'wake_lateness_ms': [(r['wake_s'] - r['target_s']) * 1000 for r in rows],
        'service_ms': [(r['finish_s'] - r['wake_s']) * 1000 for r in rows],
        'response_ms': [(r['finish_s'] - r['target_s']) * 1000 for r in rows],
        'deadline_overrun_ms': [max(0, (r['finish_s'] - r['target_s'] - 1/30) * 1000) for r in rows],
    }
    return {'samples': len(rows), 'completion_overrun_gt_1ms': sum(v > 1 for v in vectors['deadline_overrun_ms']),
            **{name: {'p50': percentile(v, 50), 'p95': percentile(v, 95), 'p99': percentile(v, 99),
                      'max': max(v), **{f'over_{n}ms': sum(x > n for x in v) for n in (50, 100, 250)}}
               for name, v in vectors.items()}}
