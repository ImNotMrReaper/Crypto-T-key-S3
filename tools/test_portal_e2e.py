#!/usr/bin/env python3
"""
Crypto TKey S3 — on-device setup-page E2E test (TEST firmware: -DTKEY_TEST_SERIAL_TOUCH).

Drives the REAL setup portal exactly like a phone would: opens it over serial, joins the key's
hotspot on wlp2s0 (marked never-default so Ethernet keeps the internet), then checks
  * live colour/effect preview reaches the physical LED for all 10 effects
  * Save answers the browser within 3 s (it used to time out) and the chosen effect persists

  ~/.venvs/tkey-tests/bin/python tools/test_portal_e2e.py [--port /dev/ttyACM1] [--iface wlp2s0]
Changes the saved home theme to orange Candle.
"""
import argparse,serial,time,re,subprocess,sys,json,colorsys
ap=argparse.ArgumentParser(); ap.add_argument("--port"); ap.add_argument("--iface",default="wlp2s0")
A=ap.parse_args()
import glob
PORT=A.port or sorted(glob.glob("/dev/ttyACM*"))[0]; IFACE=A.iface; BASE="http://10.77.0.1"
s=serial.Serial(); s.port=PORT; s.baudrate=115200; s.timeout=0.2; s.rts=False; s.dtr=True; s.open()
def ser(c,key,t=1.0):
    s.reset_input_buffer(); s.write((c+"\n").encode()); t0=time.time(); b=""
    while time.time()-t0<t and key not in b: b+=s.read(4096).decode(errors="replace")
    time.sleep(0.05); b+=s.read(4096).decode(errors="replace")
    m=[l for l in b.splitlines() if l.startswith(key)]; return m[0] if m else ""
def join_portal():
    ser("portal","[TEST]",2); time.sleep(4)
    m=re.search(r"ssid=(\S+) pass=(\S+) session=(\S+) csrf=(\S+)",ser("portalinfo","PORTAL",2))
    if not m: sys.exit("portal did not start")
    ssid,pw,sess,csrf=m.groups()
    for attempt in range(6):
        subprocess.run(["nmcli","dev","wifi","rescan","ifname",IFACE],capture_output=True); time.sleep(5)
        r=subprocess.run(["nmcli","dev","wifi","connect",ssid,"password",pw,"ifname",IFACE],capture_output=True,text=True)
        if r.returncode==0: break
    else: sys.exit("could not join the key's hotspot")
    subprocess.run(["nmcli","connection","modify",ssid,"ipv4.never-default","yes","ipv6.never-default","yes"],capture_output=True)
    return sess,csrf
SESS,CSRF=join_portal()
def curl(path,data=None,timing=False):
    a=["curl","-s","-m","8","--interface",IFACE,"-b",f"tk={SESS}","-H",f"X-TKey-CSRF: {CSRF}"]
    if timing: a+=["-w","\nHTTP %{http_code} %{time_total}"]
    for k,v in (data or {}).items(): a+=["--data-urlencode",f"{k}={v}"]
    return subprocess.run(a+[BASE+path],capture_output=True,text=True).stdout
def leds(n=8,gap=0.25):
    out=[]
    for i in range(n):
        m=re.search(r"led=(\d+),(\d+),(\d+)@(\d+) mode=(\d+)",ser("diag","DIAG",0.6))
        if m: out.append(tuple(map(int,m.groups())))
        time.sleep(gap)
    return out
def hue(r,g,b): return round(colorsys.rgb_to_hsv(r/255,g/255,b/255)[0]*360) if max(r,g,b)>8 else None
names=["Steady","Breathe","Rainbow","Heartbeat","Candle","Aurora","Ocean","Sparkle","Comet","Cycle"]
results=[]; ok_all=True
# 1) live preview, colour = green (hue 120)
for fx in range(10):
    r=curl("/api/theme_preview",{"theme_rgb":"00ff00","theme_fx":fx,"theme_speed":3,"theme_bright":1})
    time.sleep(0.4); L=leds()
    modes={x[4] for x in L}; hues=[hue(*x[:3]) for x in L]; lv=[max(x[:3]) for x in L]
    hs=[h for h in hues if h is not None]
    colorful = fx in (2,9)            # rainbow/cycle may leave green
    greenish = all(abs(((h-120)+180)%360-180)<=50 for h in hs) if hs else False
    varies = len(set(lv))>1 or len(set(hs))>1
    expect_vary = fx!=0
    ok = r.strip().startswith('{"ok":true') and modes=={2} and (colorful or greenish) and (varies==expect_vary or fx in (7,))
    ok_all&=ok
    print(f"{'PASS' if ok else 'FAIL'} preview {names[fx]:9} mode={sorted(modes)} hues={hs} levels={lv}")
# 2) save Candle in orange and confirm it persists after the preview is gone
st=json.loads(curl("/api/state"))
coins=",".join(c[0] for c in st["coins"] if c[6])
pol=st.get("policy",{})
r=curl("/api/save",timing=True,data={"coins":coins,"theme_rgb":"ff8800","theme_fx":4,"theme_speed":3,"theme_bright":1,"theme_custom_idx":-1,
      "pol_duress":pol.get("duress","wipe"),"pol_panic":pol.get("panic","wipe"),"pol_lockout":pol.get("lockout","wipe"),"pol_countdown":pol.get("countdown",3)})
m=re.search(r"HTTP (\d+) ([\d.]+)",r); code,secs=(m.group(1),float(m.group(2))) if m else ("000",99)
ok=code=="200" and secs<3 and '"ok":true' in r; ok_all&=ok
print(f"{'PASS' if ok else 'FAIL'} Save answers the browser: HTTP {code} in {secs:.2f}s")
time.sleep(3)
th=ser("theme","THEME"); print(th)
ok=("fx=4" in th and "saved=ff8800" in th and "preview=0" in th); ok_all&=ok
print(f"{'PASS' if ok else 'FAIL'} saved Candle/orange persisted (not rainbow)")
print("ALL PASS" if ok_all else "SOME FAILED")
s.close()
