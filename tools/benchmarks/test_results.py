"""Protect counterbalanced pairing from time-order and sign mistakes."""
import unittest
from results import paired_effect

class PairingTests(unittest.TestCase):
    def test_reversed_pairs_keep_B_minus_A_sign(self):
        flags=[False,True,True,False,True,False,False,True]
        values=[10,12,23,20,34,30,40,45]
        rows=[{'B_low_priority':b,'latency':v} for b,v in zip(flags,values)]
        r=paired_effect(rows,'latency')
        self.assertEqual(r['paired_B_minus_A'],[2,3,4,5])
        self.assertEqual(r['mean_delta'],3.5)
        self.assertEqual(r['mean_B']-r['mean_A'],3.5)
        self.assertLess(r['approx_95pct_t_interval'][0],3.5)
        self.assertGreater(r['approx_95pct_t_interval'][1],3.5)

    def test_incomplete_or_wrong_order_rejected(self):
        with self.assertRaises(ValueError): paired_effect([], 'latency')
        with self.assertRaises(ValueError):
            paired_effect([{'B_low_priority':False,'latency':1}]*8, 'latency')

if __name__=='__main__': unittest.main()
