import unittest
from analyze import percentile, summarize

class AnalysisTests(unittest.TestCase):
    def rows(self):
        return [{'target_s': i / 30, 'wake_s': i / 30 + .001, 'finish_s': i / 30 + .009} for i in range(100)]
    def test_known_percentiles(self):
        self.assertEqual(percentile(list(range(1, 101)), 99), 99)
        self.assertEqual(percentile(list(range(1, 101)), 50), 50)
    def test_known_deadline(self):
        s = summarize(self.rows(), expected=100)
        self.assertAlmostEqual(s['service_ms']['p50'], 8)
        self.assertEqual(s['completion_overrun_gt_1ms'], 0)
    def test_injected_stalls(self):
        rows = self.rows(); rows[20]['finish_s'] += .3; rows[70]['finish_s'] += .1
        s = summarize(rows)
        self.assertEqual(s['completion_overrun_gt_1ms'], 2)
        self.assertEqual(s['response_ms']['over_250ms'], 1)
        self.assertAlmostEqual(s['response_ms']['p99'], 109)
    def test_missing(self):
        for rows in ([], self.rows()[:-1]):
            with self.assertRaises(ValueError): summarize(rows, expected=100)
    def test_invalid(self):
        for key, value in [('target_s', float('nan')), ('wake_s', -1), ('finish_s', -1)]:
            rows = self.rows(); rows[0][key] = value
            with self.assertRaises(ValueError): summarize(rows)
    def test_failure(self):
        with self.assertRaises(ValueError): summarize(self.rows(), failed=True)

if __name__ == '__main__': unittest.main()
