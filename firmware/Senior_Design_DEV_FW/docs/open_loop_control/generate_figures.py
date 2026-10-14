"""Regenerate five standalone vector figures. Python 3 plus reportlab.
PDF and SVG share the same drawing primitives. No motor simulation is implied.
"""
from pathlib import Path
import math
import html
from reportlab.pdfgen import canvas
from reportlab.lib.colors import HexColor
OUT = Path(__file__).resolve().parent / 'figures'
OUT.mkdir(exist_ok=True)
BLUE='#245A81'; GREEN='#27745D'; GOLD='#9B6329'; GRAY='#52616B'; LIGHT='#EDF3F7'; INK='#18252D'
class Figure:
    def __init__(self,name,w,h):
        self.name,self.w,self.h=name,w,h
        self.pdf=canvas.Canvas(str(OUT/(name+'.pdf')),pagesize=(w,h))
        self.pdf.setTitle(name.replace('_',' '))
        self.svg=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}pt" height="{h}pt" viewBox="0 0 {w} {h}">', '<rect width="100%" height="100%" fill="white"/>']
    def line(self,pts,color=INK,width=1,dash=False):
        p=self.pdf.beginPath();p.moveTo(pts[0][0],self.h-pts[0][1])
        for x,y in pts[1:]:p.lineTo(x,self.h-y)
        self.pdf.setStrokeColor(HexColor(color));self.pdf.setLineWidth(width);self.pdf.setDash([4,3] if dash else [])
        self.pdf.drawPath(p);self.pdf.setDash([])
        coords=' '.join(f'{x:.3f},{y:.3f}' for x,y in pts)
        d=' stroke-dasharray="4 3"' if dash else ''
        self.svg.append(f'<polyline points="{coords}" fill="none" stroke="{color}" stroke-width="{width}"{d}/>')
    def arrow(self,pts,color=INK,dash=False):
        self.line(pts,color,1.15,dash)
        a,b=pts[-2],pts[-1];angle=math.atan2(b[1]-a[1],b[0]-a[0]);r=5
        self.line([(b[0]-r*math.cos(angle-.5),b[1]-r*math.sin(angle-.5)),b,(b[0]-r*math.cos(angle+.5),b[1]-r*math.sin(angle+.5))],color,1.15)
    def rect(self,x,y,w,h,fill=LIGHT,stroke=BLUE):
        self.pdf.setFillColor(HexColor(fill));self.pdf.setStrokeColor(HexColor(stroke));self.pdf.setLineWidth(.8)
        self.pdf.roundRect(x,self.h-y-h,w,h,5,fill=1,stroke=1)
        self.svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="5" fill="{fill}" stroke="{stroke}" stroke-width=".8"/>')
    def text(self,x,y,s,size=10,color=INK,anchor='middle',bold=False):
        # y is the first line baseline; SVG and PDF use the same line spacing.
        font='Helvetica-Bold' if bold else 'Helvetica';self.pdf.setFont(font,size);self.pdf.setFillColor(HexColor(color))
        for i,t in enumerate(s.split('\n')):
            yy=y+i*size*1.28
            if anchor=='middle':self.pdf.drawCentredString(x,self.h-yy,t)
            elif anchor=='end':self.pdf.drawRightString(x,self.h-yy,t)
            else:self.pdf.drawString(x,self.h-yy,t)
            weight='bold' if bold else 'normal'
            self.svg.append(f'<text x="{x}" y="{yy}" font-family="Arial, Helvetica, sans-serif" font-size="{size}" font-weight="{weight}" text-anchor="{anchor}" fill="{color}">{html.escape(t)}</text>')
    def box(self,x,y,w,h,label,sub=''):
        self.rect(x,y,w,h);self.text(x+w/2,y+17,label,10,bold=True)
        if sub:self.text(x+w/2,y+32,sub,9)
    def done(self):
        self.pdf.showPage();self.pdf.save();self.svg.append('</svg>');(OUT/(self.name+'.svg')).write_text('\n'.join(self.svg),encoding='utf-8')
# 1 Signal flow and separate supervisory inputs.
f=Figure('01_system_flow',510,345)
f.text(10,15,'Signal flow in the physical open-loop profile',12,anchor='start',bold=True)
f.box(12,38,132,57,'PA3 / ADC1','0-3 V command\n16-bit sample + age')
f.box(189,38,132,57,'Signed speed map','-100 to +100 RPM\n1.3-1.7 V neutral')
f.box(366,38,132,57,'Field generator','10 RPM/s ramp\nsigned electrical angle')
f.arrow([(144,65),(189,65)]);f.arrow([(321,65),(366,65)])
f.box(366,127,132,57,'Sine modulation','three duty commands\nfixed 2 V run vector')
f.box(189,127,132,57,'TIM1 hardware','20 kHz PWM pairs\n500 ns deadtime')
f.box(12,127,132,57,'DRV8304H + FETs','six logic inputs\nswitched 24 V bus')
f.arrow([(432,95),(432,127)]);f.arrow([(366,155),(321,155)]);f.arrow([(189,155),(144,155)])
f.box(12,221,132,57,'Motor windings','phase currents\nrotating magnetic field')
f.box(189,221,132,57,'Permanent magnets','torque on the rotor\nfollowing if synchronized')
f.arrow([(78,184),(78,221)]);f.arrow([(144,249),(189,249)])
f.rect(366,221,132,70,fill='#F5F5F5',stroke=GRAY);f.text(432,238,'Supervisor',10,bold=True);f.text(432,253,'PG2 RUN + neutral dwell\nADC / timing / watchdog\nPE6 nFAULT break',8.5)
f.arrow([(432,221),(432,201),(255,201),(255,184)],GRAY,True)
f.arrow([(498,247),(505,247),(505,25),(432,25),(432,38)],GRAY,True)
f.text(12,314,'Solid: command / power effect. Dashed: authorization or inhibition.',9,anchor='start')
f.text(12,331,'No rotor angle, measured speed or phase-current feedback enters this path.',9,anchor='start',bold=True)
f.done()
# 2 State graph. WAIT is application state, not motor_state_t.
f=Figure('02_state_machine',510,395)
f.text(10,16,'Startup and motor states',12,anchor='start',bold=True)
f.box(12,40,130,54,'DISABLED','ENABLE=0, PWM=0')
f.box(190,40,130,54,'WAIT FOR NEUTRAL','application flag\nENABLE=0, PWM=0')
f.box(368,40,130,54,'OPEN_IDLE','armed, drive off')
f.arrow([(142,66),(190,66)]);f.text(166,39,'fresh RUN',8)
f.arrow([(320,66),(368,66)]);f.text(344,29,'center valid',8);f.text(344,39,'for 200 ms',8)
f.box(368,130,130,54,'OPEN_WAKE','ENABLE=1, PWM=0')
f.arrow([(433,94),(433,130)]);f.text(429,115,'nonzero target',8,anchor='end')
f.box(368,220,130,54,'OPEN_ALIGN','fixed angle, 1 V ramp')
f.arrow([(433,184),(433,220)]);f.text(429,204,'10 ms, nFAULT high',8,anchor='end')
f.box(190,220,130,54,'OPEN_RUNNING','signed speed / angle ramp')
f.arrow([(368,247),(320,247)]);f.text(344,239,'2 s',8)
f.box(12,220,130,54,'OPEN_COAST','ENABLE=0, PWM=0')
f.arrow([(190,247),(142,247)]);f.text(166,203,'center / reverse:',8);f.text(166,214,'ramp reaches zero',8)
f.arrow([(77,220),(77,111),(350,111),(350,66),(368,66)])
f.text(165,106,'1 s disabled pause, then idle',8)
f.arrow([(433,274),(433,297),(77,297),(77,274)],GRAY)
f.text(255,292,'center or opposite sign during alignment',8,color=GRAY)
f.text(12,324,'RUN LOW / stop: disable and cancel waiting from every stage.',9,anchor='start',bold=True)
f.rect(190,345,130,38,fill='#F7EEEE',stroke='#925050');f.text(255,361,'FAULT',10,bold=True);f.text(255,375,'latched, drive off',8)
f.text(12,360,'Any latched fault',9,anchor='start');f.arrow([(105,356),(190,356)],'#925050')
f.text(337,358,'Recover: fix cause, RUN LOW,',8,anchor='start');f.text(337,371,'RESET, fresh RUN + center.',8,anchor='start')
f.done()
# 3 Fast switching versus slow voltage references.
f=Figure('03_pwm_and_phase_voltages',510,380)
f.text(10,16,'Fast bridge switching and slow three-phase voltage references',12,anchor='start',bold=True)
f.text(10,39,'One half bridge',10,anchor='start',bold=True)
f.line([(65,53),(65,70)]);f.text(65,49,'24 V bus',9)
f.rect(43,70,44,31,fill='#F5F5F5',stroke=GRAY);f.text(65,89,'QH',10)
f.line([(65,101),(65,126),(151,126)]);f.text(155,123,'SHx to winding',9,anchor='start');f.text(65,121,'',8)
f.line([(65,126),(65,149)]);f.rect(43,149,44,31,fill='#F5F5F5',stroke=GRAY);f.text(65,168,'QL',10)
f.line([(65,180),(65,195)]);f.text(65,209,'PGND via shunt',9)
f.text(8,93,'GHx',8,anchor='start');f.line([(30,90),(43,90)]);f.text(8,172,'GLx',8,anchor='start');f.line([(30,168),(43,168)])
f.text(270,39,'Logic inputs to the driver',10,anchor='start',bold=True)
x0,x1=290,495
for y,label in [(68,'INH'),(115,'INL'),(162,'SHx')]:
    f.text(280,y+7,label,9,anchor='end');f.line([(x0,y+20),(x1,y+20)],'#D4DCE0',.6)
f.line([(290,88),(315,88),(315,68),(390,68),(390,88),(495,88)],BLUE,1.6)
f.line([(290,115),(307,115),(307,135),(398,135),(398,115),(495,115)],GREEN,1.6)
f.line([(290,182),(315,182),(315,162),(390,162),(390,182),(495,182)],GOLD,1.6)
for a,b in [(307,315),(390,398)]:f.line([(a,56),(a,145)],GRAY,.6,True);f.line([(b,56),(b,145)],GRAY,.6,True)
f.text(392,202,'Both LOW gaps: nominal 500 ns',8.5)
f.text(392,215,'SHx approximately 0 or 24 V; deadtime details omitted.',7.5)
f.text(10,244,'Average voltage references over one electrical turn',10,anchor='start',bold=True)
left,top,width,height=45,259,435,70
f.line([(left,top+height/2),(left+width,top+height/2)],'#C9D1D6',.6)
for offset,color,label in [(0,BLUE,'A'),(-2*math.pi/3,GREEN,'B'),(2*math.pi/3,GOLD,'C')]:
    pts=[(left+width*i/240,top+height/2-30*math.cos(2*math.pi*i/240+offset)) for i in range(241)]
    f.line(pts,color,1.5)
f.text(30,265,'+2 V',8,anchor='end');f.text(30,330,'-2 V',8,anchor='end')
f.text(45,345,'0',8);f.text(262.5,345,'180 electrical degrees',8);f.text(480,345,'360',8)
f.text(390,258,'A',9,BLUE,bold=True);f.text(423,258,'B',9,GREEN,bold=True);f.text(456,258,'C',9,GOLD,bold=True)
f.text(10,369,'Envelopes differ by 120 electrical degrees; all PWM carriers are synchronized.',9,anchor='start')
f.done()
# 4 Mapping, including endpoints and center dwell significance.
f=Figure('04_analog_speed_map',510,235)
f.text(10,16,'Pot voltage to signed field-speed command',12,anchor='start',bold=True)
left,right,top,bottom=65,480,43,185
x=lambda v:left+(right-left)*v/3
y=lambda rpm:(top+bottom)/2-(bottom-top)*rpm/200
f.rect(x(1.3),top,x(1.7)-x(1.3),bottom-top,fill='#E7F2ED',stroke='#D2E3D9')
for rpm in [-100,-50,0,50,100]:
    f.line([(left,y(rpm)),(right,y(rpm))],'#D9DFE3',.6);f.text(left-9,y(rpm)+3,str(rpm),9,anchor='end')
f.line([(left,top),(left,bottom),(right,bottom)],INK,1)
f.line([(x(0),y(-100)),(x(1.3),y(0)),(x(1.7),y(0)),(x(3),y(100))],BLUE,2)
for v in [0,.65,1.3,1.7,2.35,3]:
    f.line([(x(v),bottom),(x(v),bottom+4)],INK,.7);f.text(x(v),bottom+17,str(v),9)
f.text(272,227,'PA3 input voltage (V)',10)
f.text(65,33,'RPM command',9,anchor='start');f.text(x(1.5),top+18,'Neutral',8)
f.text(137,124,'76.923 RPM/V',8,BLUE);f.text(409,104,'76.923 RPM/V',8,BLUE)
f.done()
# 5 Commanded speed, ENABLE/PWM on reversal. Times are approximate.
f=Figure('05_reversal_timing',510,255)
f.text(10,16,'Example full-scale reversal from +100 to -100 RPM',12,anchor='start',bold=True)
left,right=62,490
x=lambda t:left+(right-left)*t/23
y=lambda rpm:110-rpm*.48
for rpm in [-100,0,100]:f.line([(left,y(rpm)),(right,y(rpm))],'#D9DFE3',.6);f.text(left-9,y(rpm)+3,str(rpm),9,anchor='end')
f.line([(left,52),(left,160),(right,160)],INK,.8)
f.line([(x(0),y(100)),(x(10),y(0)),(x(13.01),y(0)),(x(23),y(-99.9))],BLUE,1.8)
for t in [0,10,11,13,23]:f.line([(x(t),160),(x(t),164)],INK,.7);f.text(x(t),177,str(t),8)
f.text(63,43,'Generated speed (RPM)',9,anchor='start')
f.text(x(4),75,'Ramp down',8,BLUE);f.text(x(10.5),39,'1 s coast',8);f.line([(x(10.5),43),(x(10.5),105)],GRAY,.6,True)
f.text(x(13),52,'2 s align + wake',8);f.line([(x(12),56),(x(12),105)],GRAY,.6,True)
f.text(x(18),126,'Reverse ramp',8,BLUE)
for base,label in [(198,'ENABLE'),(222,'PWM')]:
    f.text(54,base+1,label,8,anchor='end')
    rise=11 if label=='ENABLE' else 11.01
    f.line([(x(0),base-8),(x(10),base-8),(x(10),base+4),(x(rise),base+4),(x(rise),base-8),(x(23),base-8)],GREEN,1.3)
f.text(275,246,'Time from opposite-direction command (s); not measured rotor speed.',8.5)
f.done()
print('Generated five vector PDF/SVG figure pairs in',OUT)
