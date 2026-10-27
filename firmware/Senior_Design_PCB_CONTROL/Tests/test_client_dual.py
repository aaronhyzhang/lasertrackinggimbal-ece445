import sys
from pathlib import Path
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Tools'))
import dual_position_sweep as sweep
class Peer:
    def __init__(self,axis,fault=False):self.axis=axis;self.commands=[];self.target=100000;self.moving=False;self.fault=fault
    def request(self,op,*args):
        self.commands.append((op,args))
        if op=='POSITION':self.target=args[0];self.moving=True;return 'OK'
        if op=='STATUS':return dict(axis=self.axis,dual=1,source=1,enabled=1,calibrated=1,zero=1,
            state=7 if self.moving else 5,fault=5 if self.fault and self.moving else 0,
            position_mdeg=self.target,target_mdeg=self.target,rpm_milli=0,settled=1)
        return 'OK'
class Tests(unittest.TestCase):
    def test_windows(self):
        for low,high in ((15,345),(15,320)):
            plan=sweep.plans(low,high,6,1)
            self.assertEqual(len(plan),17);self.assertTrue(all(low<=x<=high for x in plan))
    def test_pair_completion_and_disable(self):
        peers=[Peer(0),Peer(1)];clock=[0]
        with patch.object(sweep.time,'monotonic',side_effect=lambda:clock[0]),patch.object(sweep.time,'sleep',side_effect=lambda dt:clock.__setitem__(0,clock[0]+dt)):
            sweep.run(peers,[[20,30],[40,50]])
        for peer in peers:
            self.assertEqual([v[1][0] for v in peer.commands if v[0]=='POSITION'],[20_000,30_000] if peer.axis==0 else [40_000,50_000])
            self.assertEqual(peer.commands[-1][0],'DISABLE')
    def test_fault_stops_pair(self):
        peers=[Peer(0),Peer(1,True)]
        with self.assertRaises(RuntimeError):sweep.run(peers,[[20],[40]])
        self.assertTrue(all(p.commands[-1][0]=='DISABLE' for p in peers))
    def test_wrong_firmware_rejected_before_motion(self):
        peer=Peer(0);original=peer.request
        def legacy(op,*args):
            v=original(op,*args)
            if isinstance(v,dict):v.pop('dual',None)
            return v
        peer.request=legacy
        with self.assertRaises(RuntimeError):sweep.run([peer,Peer(1)],[[20],[40]])
        self.assertFalse(any(x[0]=='POSITION' for x in peer.commands))
