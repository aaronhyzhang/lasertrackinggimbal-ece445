import contextlib
import importlib.util
import io
from pathlib import Path
import socket
import sys
import threading
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('position_client',Path(__file__).resolve().parents[1]/'Tools/position_client.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)


class Peer:
    def __init__(self,settle=True,axis=0):
        self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.sock.bind(('127.0.0.1',0));self.sock.settimeout(0.1)
        self.port=self.sock.getsockname()[1];self.commands=[]
        self.settle=settle;self.axis=axis;self.running=False;self.target=0;self.done=False
        self.thread=threading.Thread(target=self.run);self.thread.start()

    def run(self):
        while not self.done:
            try:
                data,peer=self.sock.recvfrom(1024)
            except socket.timeout:
                continue
            fields=data.decode().split();op=fields[3];self.commands.append(op)
            if op=='STATUS':
                reply=(f'S1 axis={self.axis} source=1 enabled=1 state={7 if self.running else 5} '
                       f'fault=0 calibrated=1 zero=1 position_mdeg={self.target} target_mdeg={self.target} '
                       f'rpm_milli=0 settled={int(self.settle and self.running)}')
            elif op=='DISABLE':
                self.running=False;reply='OK DISABLED'
            else:
                if op=='POSITION':self.target=int(fields[4])
                if op=='HEARTBEAT':self.running=True
                reply=f'OK G1 {fields[1]} {fields[2]}'
            self.sock.sendto(reply.encode(),peer)

    def close(self):
        self.done=True;self.thread.join();self.sock.close()


class Tests(unittest.TestCase):
    def invoke(self,peer,*args):
        with patch.object(sys,'argv',['client','--host','127.0.0.1','--port',str(peer.port),*args]),contextlib.redirect_stdout(io.StringIO()) as output:
            module.main()
        return output.getvalue()

    def test_actual_udp_motion_and_disable(self):
        peer=Peer()
        try:
            output=self.invoke(peer,'--position','90')
            self.assertIn('PASS: measured',output)
            self.assertIn('POSITION',peer.commands);self.assertEqual(peer.commands[-1],'DISABLE')
        finally:peer.close()

    def test_ack_without_settling_fails_and_stops(self):
        peer=Peer(settle=False)
        try:
            with self.assertRaises(TimeoutError):self.invoke(peer,'--position','90','--timeout','0.12')
            self.assertEqual(peer.commands[-1],'DISABLE')
        finally:peer.close()

    def test_status_only_does_not_stop(self):
        peer=Peer()
        try:
            self.invoke(peer);self.assertEqual(peer.commands,['STATUS'])
        finally:peer.close()

    def test_axis_mismatch_never_commands_motion(self):
        peer=Peer(axis=1)
        try:
            with self.assertRaisesRegex(RuntimeError,'axis'):self.invoke(peer,'--position','90')
            self.assertEqual(peer.commands,['STATUS'])
        finally:peer.close()

    def test_bad_status_rejected(self):
        with self.assertRaises(ValueError):module.status_fields('OK G1 1 2')


if __name__=='__main__':unittest.main()
