#pragma once
// Setup page served by WebPortal. Self-contained (the setup AP has no internet).
// Data comes from /api/state; nothing secret is ever rendered back into it.
// Preview locally with tools/preview_portal.py.

static const char PORTAL_PAGE[] PROGMEM = R"PAGE(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="dark">
<title>T-Key setup</title>
<style>
:root{--void:#000;--line:#221f2c;--field:#0d0c13;--text:#edebf4;--muted:#8d89a1;--reaper:#7764d8;--reaper-soft:rgba(119,100,216,.18);--ok:#34c77b;--warn:#f0a93b;--bad:#ff5a52;--home:#7764d8;
--round:ui-rounded,"SF Pro Rounded","Nunito","Varela Round",system-ui,sans-serif;--body:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
*{box-sizing:border-box}
html{background:var(--void)}
body{margin:0;color:var(--text);font:16px/1.5 var(--body);font-variant-numeric:tabular-nums;padding:0 20px 120px;-webkit-text-size-adjust:100%}
main{max-width:520px;margin:0 auto}
h1,h2{font-family:var(--round);font-weight:700;letter-spacing:-.01em;margin:0}
h1{font-size:28px;line-height:1.15}
h2{font-size:20px;margin-bottom:4px}
p{margin:0}
.lede{color:var(--muted);margin-top:8px;max-width:38ch}
section{padding:28px 0;border-top:1px solid var(--line)}
section>.why{color:var(--muted);font-size:14px;margin-bottom:14px;max-width:44ch}
label.f{display:block;font-size:14px;color:var(--muted);margin:16px 0 6px}
input[type=text],input[type=password],input[type=search],select{width:100%;background:var(--field);color:var(--text);border:1px solid var(--line);border-radius:12px;padding:12px 14px;font:inherit}
input::placeholder{color:#5d5970}
input:focus-visible,select:focus-visible,button:focus-visible,.seg button:focus-visible,.sw:focus-within{outline:2px solid var(--reaper);outline-offset:2px}
.pair{display:grid;gap:8px}
.note{font-size:13px;color:var(--muted);margin-top:6px}
.caution{font-size:13px;color:var(--warn);margin-top:6px}
button{font:600 16px/1 var(--body);border:0;border-radius:12px;padding:14px 16px;color:#fff;background:var(--reaper);cursor:pointer}
button.quiet{background:transparent;color:var(--text);border:1px solid var(--line)}
button.link{background:none;padding:6px 0;color:var(--reaper);font-weight:600;font-size:14px}
button:disabled{opacity:.5}

/* Hero: the key itself */
.hero{padding:28px 0 24px}
.key{position:relative;margin:4px 0 22px;display:flex;align-items:center;max-width:340px}
/* LilyGO T-Dongle-S3: USB-A plug, black ABS shell, 0.96" 160x80 IPS window, BOOT key, LED light pipe, TF slot */
.plug{width:44px;height:30px;background:linear-gradient(#e4e5ea,#9a9ca5 55%,#c9cad0);border-radius:2px 0 0 2px;position:relative;box-shadow:inset 0 0 0 1px #7d7f88}
.plug::before{content:"";position:absolute;left:6px;right:0;top:9px;height:12px;background:#1d1c22;border-radius:1px}
.plug::after{content:"";position:absolute;left:10px;top:12px;width:22px;height:6px;background:repeating-linear-gradient(90deg,#d9b25a 0 3px,transparent 3px 6px)}
.body{flex:1;background:linear-gradient(180deg,#1b1a20,#0b0a0e);border:1px solid #2a2733;border-radius:5px 16px 16px 5px;padding:9px 14px 9px 12px;display:flex;align-items:center;gap:11px;position:relative;box-shadow:0 10px 30px #0008,inset 0 1px 0 #ffffff14}
.body::before{content:"";position:absolute;top:-4px;left:34%;width:18px;height:4px;background:#2d2b35;border-radius:2px 2px 0 0}  /* BOOT button */
.body::after{content:"";position:absolute;right:3px;top:50%;width:3px;height:16px;margin-top:-8px;background:#050507;border-radius:2px}  /* microSD slot */
.screen{aspect-ratio:2/1;flex:1;background:#000;border-radius:3px;border:1px solid #2c2936;padding:7% 8%;display:flex;flex-direction:column;justify-content:space-between;overflow:hidden}
.screen .top{display:flex;justify-content:space-between;font:700 11px/1 var(--round);color:var(--home)}
.screen .big{font:800 clamp(18px,6vw,24px)/1 var(--round);color:#fff}
.screen .chips{display:flex;gap:3px;flex-wrap:nowrap;overflow:hidden;height:7px}
.screen .chips i{width:7px;height:7px;border-radius:50%;flex:0 0 7px}
.led{width:14px;height:14px;border-radius:50%;background:var(--home);box-shadow:0 0 14px 3px var(--home);flex:0 0 14px}
.led.breathe{animation:br 3.2s ease-in-out infinite}
.led.rainbow{animation:rb 6s linear infinite}
@keyframes br{0%,100%{opacity:.3;box-shadow:0 0 4px 0 var(--home)}50%{opacity:1;box-shadow:0 0 16px 4px var(--home)}}
@keyframes rb{0%{background:#ff3b30;box-shadow:0 0 14px 3px #ff3b30}20%{background:#ffcc00;box-shadow:0 0 14px 3px #ffcc00}40%{background:#34c759;box-shadow:0 0 14px 3px #34c759}60%{background:#00c7ff;box-shadow:0 0 14px 3px #00c7ff}80%{background:#7764d8;box-shadow:0 0 14px 3px #7764d8}100%{background:#ff3b30;box-shadow:0 0 14px 3px #ff3b30}}
@media (prefers-reduced-motion:reduce){.led.breathe,.led.rainbow{animation:none}}

/* Coins */
.tools{display:flex;gap:6px;flex-wrap:wrap;margin:12px 0 4px}
.seg{display:flex;background:var(--field);border:1px solid var(--line);border-radius:12px;padding:3px;gap:2px;flex-wrap:wrap}
.seg button{flex:1;background:none;color:var(--muted);font-size:14px;padding:9px 10px;border-radius:9px;white-space:nowrap}
.seg button[aria-pressed=true]{background:var(--reaper-soft);color:var(--text)}
.tally{font-size:14px;color:var(--muted);margin:12px 0 2px}
.tally b{color:var(--text)}
.group{margin-top:18px}
.group>p{font-size:13px;color:var(--muted);margin-bottom:4px}
.coin{display:flex;align-items:center;gap:12px;padding:8px 0;border-bottom:1px solid #15131c;cursor:pointer}
.chip{width:14px;height:14px;border-radius:50%;flex:0 0 14px;border:2px solid var(--c);background:transparent;transition:background .15s}
.coin.on .chip{background:var(--c);box-shadow:0 0 8px var(--c)}
.coin .who{flex:1;min-width:0;display:flex;align-items:baseline;gap:8px;overflow:hidden}
.coin .who b{font:700 16px/1.2 var(--round)}
.coin .who span{color:var(--muted);font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.coin .net{color:var(--muted);font-size:12px;white-space:nowrap}
.coin .kind{font-size:12px;white-space:nowrap}
.kind.stable{color:var(--ok)}.kind.meme{color:var(--warn)}
.sw{position:relative;width:44px;height:26px;flex:0 0 44px}
.sw input{position:absolute;inset:0;opacity:0;margin:0;cursor:pointer}
.sw span{position:absolute;inset:0;background:#2a2733;border-radius:13px;transition:background .15s}
.sw span::after{content:"";position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:#fff;transition:transform .15s}
.sw input:checked+span{background:var(--reaper)}
.sw input:checked+span::after{transform:translateX(18px)}

/* Home look & Theme Studio */
.swatches{display:flex;gap:10px;flex-wrap:wrap;margin:8px 0 14px}
.swatch{width:38px;height:38px;border-radius:50%;border:2px solid transparent;padding:0;background:var(--s);cursor:pointer;flex:0 0 38px;-webkit-tap-highlight-color:transparent}
.swatch[aria-pressed=true]{border-color:#fff;box-shadow:0 0 8px #fff}
.cm-card{display:flex;justify-content:space-between;align-items:center;background:var(--field);border:1px solid var(--line);border-radius:10px;padding:10px 12px;margin-bottom:8px}
.cm-card.active{border-color:var(--home);box-shadow:0 0 8px var(--reaper-soft)}
.cm-chips{display:flex;gap:6px;align-items:center}
.cm-chips i{display:inline-block;width:16px;height:16px;border-radius:50%;border:1px solid rgba(255,255,255,0.4)}

/* Wi-Fi */
.net-row{display:flex;justify-content:space-between;align-items:center;padding:10px 0;border-bottom:1px solid #15131c}
.net-row.gone span{text-decoration:line-through;color:var(--muted)}
.net-row small{color:var(--reaper);margin-left:8px}
.inline{display:flex;gap:8px}.inline input{flex:1}

/* Footer */
.bar{position:fixed;left:0;right:0;bottom:0;background:rgba(0,0,0,.94);backdrop-filter:blur(8px);border-top:1px solid var(--line);padding:12px 20px calc(12px + env(safe-area-inset-bottom))}
.bar .in{max-width:520px;margin:0 auto;display:flex;gap:10px}.bar .grow{flex:1}
#msg{font-size:14px;min-height:20px;margin-bottom:10px;max-width:520px;margin-left:auto;margin-right:auto}
#msg.bad{color:var(--bad)}#msg.ok{color:var(--ok)}
.hide{display:none!important}
</style></head><body><main>

<header class="hero">
  <div class="key" aria-hidden="true">
    <div class="plug"></div>
    <div class="body">
      <div class="screen"><div class="top"><span id="scrOwner">T-KEY S3</span><span id="scrCount"></span></div><div class="big" id="scrBig">--:--</div><div class="chips" id="scrChips"></div></div>
      <div class="led" id="led"></div>
    </div>
  </div>
  <h1 id="title">Set up your T-Key</h1>
  <p class="lede" id="lede">Choose a PIN, pick your coins and give the key its look. Nothing is saved until you tap Save to key.</p>
</header>

<section id="login" class="hide">
  <h2>Enter the setup password</h2>
  <p class="why">This key is already set up. The setup password you chose protects these settings.</p>
  <label class="f" for="lp">Setup password</label>
  <input type="password" id="lp" autocomplete="current-password">
  <p class="caution hide" id="lock"></p>
  <div style="height:14px"></div>
  <button id="lb">Unlock settings</button>
</section>

<div id="app" class="hide">
<section>
  <h2>Protect the key</h2>
  <p class="why">You type the PIN on the key itself to open the wallet. The setup password guards this page.</p>
  <label class="f" for="pin">PIN, 4 to 8 digits</label>
  <div class="pair"><input type="password" id="pin" inputmode="numeric" autocomplete="new-password" maxlength="8" placeholder="New PIN"><input type="password" id="pin2" inputmode="numeric" autocomplete="new-password" maxlength="8" placeholder="Repeat PIN"></div>
  <p class="note" id="pinHint"></p>
  <label class="f" for="du">Duress PIN, optional (same number of digits as your PIN)</label>
  <input type="password" id="du" inputmode="numeric" autocomplete="new-password" maxlength="8" placeholder="Leave empty for none">
  <p class="caution">Typing the duress PIN on the key runs your emergency policy below (by default it erases the key). It must be as long as your PIN, so nobody watching can tell the two apart. Set one only if your recovery words are written down.</p>
  <label class="note hide" id="duClearRow" style="display:flex;gap:8px;align-items:center;margin-top:10px"><input type="checkbox" id="duClear" style="width:18px;height:18px;accent-color:var(--reaper)"> Remove the duress PIN that is set now</label>
  <label class="f" for="sp">Setup password, at least 8 characters</label>
  <div class="pair"><input type="password" id="sp" autocomplete="new-password" placeholder="New setup password"><input type="password" id="sp2" autocomplete="new-password" placeholder="Repeat setup password"></div>
  <p class="note" id="spHint"></p>
  <p class="note" style="margin-top:18px">Your 12 recovery words are made and shown only on the key's own screen, never on this page.</p>
</section>
<section>
  <h2>Emergency policy</h2>
  <p class="why">Choose what the key does in an emergency. Security screens always look the same, so an emergency action can't be spotted.</p>
  <label class="f" for="polDuress">Duress PIN trigger</label>
  <select id="polDuress"></select>
  <p class="note" id="hDuress"></p>
  <label class="f" for="polPanic">Panic hold trigger (&gt;6 s button hold)</label>
  <select id="polPanic"></select>
  <p class="note" id="hPanic"></p>
  <label class="f" for="polLockout">PIN lockout trigger (10 failed PIN attempts)</label>
  <select id="polLockout"></select>
  <p class="note" id="hLockout"></p>
  <label class="f" for="polCountdown">Panic countdown duration</label>
  <select id="polCountdown"></select>
  <p class="note">A button tap during the panic countdown cancels the action.</p>
</section>
<section id="secSd">
  <h2>MicroSD card vault</h2>
  <p class="why" id="sdStatus">Checking card status…</p>
  <div id="sdActions" class="hide">
    <!-- Card 1: Full Backup (This key only) -->
    <div style="margin-top:14px;background:var(--field);border:1px solid var(--line);border-radius:12px;padding:14px">
      <h3 style="font-size:15px;margin:0 0 4px">Full backup: everything, opens only on THIS key</h3>
      <p class="why" style="margin:0 0 8px">Silicon-bound hardware encryption. Backs up wallet keys, passkeys, PIN, and settings to <code>/vault/full.tkb</code>. Opens only on this physical device, locked with your setup password.</p>
      <div id="sdFullStatus" class="note" style="margin-bottom:8px"></div>
      <div style="display:flex;gap:10px;flex-wrap:wrap">
        <button type="button" class="quiet" id="btnSdFullBackup" style="flex:1;min-width:140px">Back up everything</button>
        <button type="button" class="quiet hide" id="btnSdFullRestore" style="flex:1;min-width:140px">Restore everything</button>
      </div>
      <div id="sdFullBackupBox" class="hide" style="margin-top:12px;border-top:1px solid var(--line);padding-top:12px">
        <label class="f" for="sdFullPw">Current setup password</label>
        <input type="password" id="sdFullPw" placeholder="Current setup password" autocomplete="current-password">
        <p class="note" style="margin-top:4px">Verified against your stored password before encryption. Derivation takes ~14 seconds.</p>
        <div style="display:flex;gap:10px;margin-top:10px">
          <button type="button" id="doSdFullBackup" style="flex:1">Create full backup</button>
          <button type="button" class="quiet" id="cancelSdFullBackup">Cancel</button>
        </div>
      </div>
      <div id="sdFullRestoreBox" class="hide" style="margin-top:12px;border-top:1px solid var(--line);padding-top:12px">
        <div id="sdFullRestWarnGroup" class="hide">
          <p class="caution">This replaces all keys, passkeys, PINs and settings on this device with the backup.</p>
          <label class="note" style="display:flex;gap:8px;align-items:center;margin-top:8px">
            <input type="checkbox" id="sdFullRestConfirm" style="width:18px;height:18px;accent-color:var(--reaper)">
            I understand this replaces current settings and reboots the key
          </label>
        </div>
        <label class="f" for="sdFullRestPw" style="margin-top:8px">Backup setup password</label>
        <input type="password" id="sdFullRestPw" placeholder="Setup password when backup was made">
        <div style="display:flex;gap:10px;margin-top:12px">
          <button type="button" id="doSdFullRestore" style="flex:1">Restore and restart</button>
          <button type="button" class="quiet" id="cancelSdFullRestore">Cancel</button>
        </div>
      </div>
    </div>

    <!-- Card 2: Disaster Copy (Wallet only, any T-Key) -->
    <div style="margin-top:14px;background:var(--field);border:1px solid var(--line);border-radius:12px;padding:14px">
      <h3 style="font-size:15px;margin:0 0 4px">Disaster copy: wallet only, opens on any T-Key with its passphrase</h3>
      <p class="why" style="margin:0 0 8px">Standard BIP-39 seed encrypted with AES-256-GCM to <code>/vault/tkey_backup.vault</code>. Restorable onto any replacement T-Key using your chosen passphrase (at least 12 characters).</p>
      <div id="sdSeedStatus" class="note" style="margin-bottom:8px"></div>
      <div style="display:flex;gap:10px;flex-wrap:wrap">
        <button type="button" class="quiet" id="btnSdBackup" style="flex:1;min-width:140px">Back up wallet seed</button>
        <button type="button" class="quiet hide" id="btnSdRestore" style="flex:1;min-width:140px">Restore wallet seed</button>
      </div>
      <div id="sdBackupBox" class="hide" style="margin-top:12px;border-top:1px solid var(--line);padding-top:12px">
        <label class="f" for="sdPin">Current device PIN</label>
        <input type="password" id="sdPin" inputmode="numeric" maxlength="8" placeholder="Current PIN">
        <label class="f" for="sdPass">Passphrase (at least 12 characters)</label>
        <input type="password" id="sdPass" placeholder="Passphrase">
        <label class="f" for="sdPass2">Repeat passphrase</label>
        <input type="password" id="sdPass2" placeholder="Repeat passphrase">
        <div style="display:flex;gap:10px;margin-top:14px">
          <button type="button" id="doSdBackup" style="flex:1">Create disaster copy</button>
          <button type="button" class="quiet" id="cancelSdBackup">Cancel</button>
        </div>
      </div>
      <div id="sdRestoreBox" class="hide" style="margin-top:12px;border-top:1px solid var(--line);padding-top:12px">
        <div id="sdRestorePinGroup">
          <label class="f" for="sdRestPin">Current device PIN</label>
          <input type="password" id="sdRestPin" inputmode="numeric" maxlength="8" placeholder="Current PIN">
        </div>
        <div id="sdReplaceWarnGroup" class="hide" style="margin-top:10px">
          <p class="caution">This replaces the wallet on this key. Make sure you have its seed words or backup.</p>
          <label class="note" style="display:flex;gap:8px;align-items:center;margin-top:8px">
            <input type="checkbox" id="sdReplaceConfirm" style="width:18px;height:18px;accent-color:var(--reaper)">
            I understand this replaces the current wallet
          </label>
        </div>
        <label class="f" for="sdRestPass" style="margin-top:8px">Backup passphrase</label>
        <input type="password" id="sdRestPass" placeholder="Passphrase">
        <div style="display:flex;gap:10px;margin-top:14px">
          <button type="button" id="doSdRestore" style="flex:1">Restore wallet seed</button>
          <button type="button" class="quiet" id="cancelSdRestore">Cancel</button>
        </div>
      </div>
    </div>

    <!-- Card 3: Erase MicroSD -->
    <div style="margin-top:14px;background:var(--field);border:1px solid #3d1b22;border-radius:12px;padding:14px">
      <div style="display:flex;justify-content:space-between;align-items:center">
        <div>
          <h3 style="font-size:15px;margin:0 0 2px;color:var(--bad)">Erase MicroSD Card</h3>
          <p class="why" style="margin:0">Overwrites and scrubs /vault/* and all .psbt files.</p>
        </div>
        <button type="button" class="quiet" id="btnSdEraseToggle" style="color:var(--bad)">Erase…</button>
      </div>
      <div id="sdEraseBox" class="hide" style="margin-top:12px;border-top:1px solid var(--line);padding-top:12px">
        <p class="caution">Overwrites each vault file and PSBT with random bytes and zeros before deleting. Flash memory wear levelling cannot mathematically guarantee complete physical erasure of remapped blocks.</p>
        <label class="f" for="sdEraseConfirm">Type ERASE to confirm</label>
        <input type="text" id="sdEraseConfirm" placeholder="ERASE" autocomplete="off" autocapitalize="characters" spellcheck="false">
        <div style="display:flex;gap:10px;margin-top:12px">
          <button type="button" id="doSdErase" style="flex:1;background:var(--bad);color:#fff">Erase SD card</button>
          <button type="button" class="quiet" id="cancelSdErase">Cancel</button>
        </div>
      </div>
    </div>
  </div>
</section>
<section>
  <h2>Lighting &amp; Theme Studio</h2>
  <p class="why">Pick a name, wallpaper, theme colour and LED effect for your T-Key while sitting idle. Changes preview live on device.</p>
  <label class="f" for="keyName">Name your key</label>
  <input type="text" id="keyName" maxlength="16" placeholder="e.g. Reaper's T-Key" autocomplete="off" autocapitalize="off" spellcheck="false" style="margin-bottom:12px">
  <label class="f" for="themeWallpaper">Home wallpaper</label>
  <select id="themeWallpaper" style="margin-bottom:14px">
    <option value="0">None (Deep Black)</option>
    <option value="1">Subtle Grid</option>
    <option value="2">Aurora Gradient</option>
    <option value="3">Starfield</option>
  </select>
  <div style="display:flex;justify-content:space-between;align-items:center;margin-top:12px">
    <span style="font-size:13px;color:var(--muted)">Presets (14)</span>
    <span id="colHex" style="font-family:monospace;font-size:13px;padding:2px 8px;border-radius:6px;background:var(--field);border:1px solid var(--line)">#7764d8</span>
  </div>
  <div class="swatches" id="sw"></div>
  <div id="pickerBox" style="background:var(--field);border:1px solid var(--line);border-radius:12px;padding:12px;margin-top:8px">
    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:10px">
      <span style="font-size:14px;font-weight:600">Custom Colour Studio</span>
      <div id="pickPreview" style="width:28px;height:28px;border-radius:50%;border:2px solid #fff;background:#7764d8"></div>
    </div>
    <div id="satValBox" style="position:relative;width:100%;height:130px;border-radius:8px;overflow:hidden;touch-action:none;cursor:crosshair;background:#7764d8">
      <div style="position:absolute;inset:0;background:linear-gradient(to right,#fff,transparent);pointer-events:none"></div>
      <div style="position:absolute;inset:0;background:linear-gradient(to top,#000,transparent);pointer-events:none"></div>
      <div id="svHandle" style="position:absolute;width:22px;height:22px;border-radius:50%;border:2px solid #fff;box-shadow:0 0 4px #000;transform:translate(-50%,-50%);pointer-events:none;left:50%;top:50%"></div>
    </div>
    <div id="hueBox" style="position:relative;width:100%;height:36px;border-radius:18px;margin-top:12px;touch-action:none;cursor:pointer;background:linear-gradient(to right,#f00 0%,#ff0 17%,#0f0 33%,#0ff 50%,#00f 67%,#f0f 83%,#f00 100%)">
      <div id="hueHandle" style="position:absolute;width:34px;height:34px;border-radius:50%;border:3px solid #fff;box-shadow:0 0 6px #000;top:1px;transform:translateX(-50%);pointer-events:none;left:50%"></div>
    </div>
  </div>
  <label class="f" style="margin-top:16px">LED Effect</label>
  <div class="seg" id="fx" role="group" aria-label="LED effect" style="margin-top:4px">
    <button data-fx="0">Steady</button>
    <button data-fx="1">Breathe</button>
    <button data-fx="2">Rainbow</button>
    <button data-fx="3">Heartbeat</button>
    <button data-fx="4">Candle</button>
    <button data-fx="5">Aurora</button>
    <button data-fx="6">Ocean</button>
    <button data-fx="7">Sparkle</button>
    <button data-fx="8">Comet</button>
    <button data-fx="9">Cycle</button>
  </div>
  <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:14px">
    <div>
      <label class="f">Effect Speed</label>
      <div class="seg" id="segSpeed" role="group" aria-label="Speed">
        <button data-spd="1">0.5x</button><button data-spd="2">0.75x</button><button data-spd="3">1.0x</button><button data-spd="4">1.5x</button><button data-spd="5">2.0x</button>
      </div>
    </div>
    <div>
      <label class="f">Brightness</label>
      <div class="seg" id="segBright" role="group" aria-label="Brightness">
        <button data-b="0">Low</button><button data-b="1">Med</button><button data-b="2">High</button>
      </div>
    </div>
  </div>
  <div style="margin-top:18px;border-top:1px solid #1a1723;padding-top:14px">
    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px">
      <div>
        <h3 style="font-size:15px;margin:0">Custom Modes (<span id="cmCountLabel">0</span>/4)</h3>
        <p class="why" style="margin:2px 0 0">Save up to 4 custom combinations.</p>
      </div>
      <button type="button" class="quiet" id="btnNewCm" style="padding:6px 10px;font-size:13px">+ Create</button>
    </div>
    <div id="cmList"></div>
    <div id="cmEditor" class="hide" style="margin-top:10px;background:var(--field);border:1px solid var(--line);border-radius:12px;padding:12px">
      <h4 style="margin:0 0 8px;font-size:14px">New Custom Mode</h4>
      <label class="f" for="cmName">Name</label>
      <input type="text" id="cmName" maxlength="15" placeholder="e.g. Neon Wave" style="margin-bottom:10px">
      <label class="f">Colours (1 to 4)</label>
      <div style="display:flex;gap:8px;align-items:center;margin:6px 0 12px">
        <div id="cmSlots" style="display:flex;gap:8px"></div>
        <button type="button" class="quiet" id="btnAddColor" style="padding:4px 8px;font-size:12px">+ Add</button>
      </div>
      <div style="display:flex;gap:10px">
        <button type="button" id="btnSaveCm" style="flex:1">Save Mode</button>
        <button type="button" class="quiet" id="btnCancelCm">Cancel</button>
      </div>
    </div>
  </div>
</section>
<section>
  <h2>Wi-Fi for wall power</h2>
  <p class="why">On a phone charger the key briefly joins Wi-Fi to refresh prices. Plugged into a computer, its radio stays off.</p>
  <div id="nets"></div>
  <label class="f" for="ws">Add a network</label>
  <div class="inline"><input type="text" id="ws" placeholder="Network name" autocapitalize="off" autocorrect="off"><button class="quiet" id="scan">Find</button></div>
  <select id="scanList" class="hide" style="margin-top:8px"></select>
  <input type="password" id="wp" placeholder="Wi-Fi password" style="margin-top:8px">
  <button class="link" id="addNet">Add network</button>
</section>
<section>
  <h2>Choose your coins</h2>
  <p class="why">Each coin you turn on gets a receive address, a live price, its own screen and its own LED rhythm. Coins on the same network share one address.</p>
  <input type="search" id="q" placeholder="Search by name, symbol or network" autocomplete="off">
  <div class="tools"><div class="seg" id="tabs" role="group" aria-label="Filter coins"></div></div>
  <p class="tally" id="tally"></p>
  <div id="list"></div>
  <div style="display:flex;gap:16px;margin-top:10px"><button class="link" id="selVis">Turn on all shown</button><button class="link" id="clrVis">Turn off all shown</button></div>
</section>
</div>
</main>

<div class="bar hide" id="bar"><p id="msg" role="status"></p><div class="in"><button class="quiet hide" id="exit">Leave without saving</button><button class="grow" id="save">Save to key</button></div></div>

<script>
const $=id=>document.getElementById(id);
let S=null,tab='all',fx=1,col='7764d8',spd=3,bright=1,activeCustom=-1,customModes=[],newNets=[],delNets=new Set();
let curH=250,curS=0.54,curV=0.85,cmTempColors=['7764d8'];
const PRESETS=['7764d8','00e5ff','30d158','ffd60a','ff375f','ff9f0a','bf5af2','0a84ff','64d2ff','5e5ce6','ac8e68','ffffff','d4ff3a','e056fd'];

let previewTimer=null,lastPreviewSend=0;
function sendThemePreview(force){
  const now=Date.now();
  if(!force&&(now-lastPreviewSend<75)){
    if(!previewTimer){
      previewTimer=setTimeout(()=>{previewTimer=null;sendThemePreview(true)},75-(now-lastPreviewSend));
    }
    return;
  }
  lastPreviewSend=now;
  const wp=$('themeWallpaper')?$('themeWallpaper').value:'0';
  api('/api/theme_preview',{theme_rgb:col,theme_fx:String(fx),theme_speed:String(spd),theme_bright:String(bright),theme_wallpaper:wp}).catch(()=>{});
}

window.addEventListener('pagehide',()=>{
  if(navigator.sendBeacon&&S&&S.csrf){
    const d=new URLSearchParams({revert:'1',csrf:S.csrf});
    navigator.sendBeacon('/api/theme_preview',d);
  }
});

function hsvToRgb(h,s,v){
  const c=v*s,x=c*(1-Math.abs((h/60)%2-1)),m=v-c;
  let r=0,g=0,b=0;
  if(h<60){r=c;g=x}
  else if(h<120){r=x;g=c}
  else if(h<180){g=c;b=x}
  else if(h<240){g=x;b=c}
  else if(h<300){r=c;b=x}
  else{r=c;b=x}
  return[Math.round((r+m)*255),Math.round((g+m)*255),Math.round((b+m)*255)];
}
function rgbToHsv(r,g,b){
  r/=255;g/=255;b/=255;
  const mx=Math.max(r,g,b),mn=Math.min(r,g,b),d=mx-mn;
  let h=0,s=mx===0?0:d/mx,v=mx;
  if(d!==0){
    if(mx===r)h=((g-b)/d)%6;
    else if(mx===g)h=(b-r)/d+2;
    else h=(r-g)/d+4;
    h=Math.round(h*60);
    if(h<0)h+=360;
  }
  return[h,s,v];
}
function hexToRgb(h){
  const n=parseInt(h,16);
  return[(n>>16)&255,(n>>8)&255,n&255];
}
function rgbToHex(r,g,b){
  return[r,g,b].map(x=>x.toString(16).padStart(2,'0')).join('');
}

function say(t,kind){const m=$('msg');m.textContent=t;m.className=kind||''}
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
async function api(p,body){
  const o=body?{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-TKey-CSRF':S&&S.csrf||''},body:new URLSearchParams(body)}:{};
  const r=await fetch(p,o);const j=await r.json().catch(()=>({error:'The key sent an unreadable reply. Reload the page.'}));
  if(!r.ok)throw new Error(j.error||('Error '+r.status));return j}
async function load(){
  try{S=await api('/api/state')}catch(e){$('bar').classList.remove('hide');say('Can\'t reach the key. Check that your phone is still on its Wi-Fi.','bad');return}
  if(!S.auth){$('login').classList.remove('hide');$('app').classList.add('hide');$('bar').classList.add('hide');
    $('title').textContent='Your T-Key';$('lede').textContent='Unlock to change its settings.';
    if(S.lockedFor>0){$('lock').textContent='Too many wrong passwords. Try again in '+Math.ceil(S.lockedFor/60)+' min.';$('lock').classList.remove('hide')}
    mirror();return}
  $('login').classList.add('hide');$('app').classList.remove('hide');$('bar').classList.remove('hide');
  if(S.provisioned){$('title').textContent='Your T-Key';$('lede').textContent='Change what you need. Nothing is saved until you tap Save to key.'}
  $('pinHint').textContent=S.hasPin?'Leave both empty to keep your current PIN.':'Needed to open the wallet on the key.';
  $('spHint').textContent=S.hasPassword?'Leave both empty to keep your current setup password.':'Needed to change these settings later.';
  $('duClearRow').classList.toggle('hide',!S.hasDuress);
  $('exit').classList.toggle('hide',!S.provisioned);
  col=S.theme.rgb;fx=S.theme.fx;
  spd=S.theme.speed||3;bright=S.theme.brightness||1;
  activeCustom=S.theme.activeCustom??-1;
  customModes=S.theme.customModes||[];
  if($('keyName')){
    $('keyName').value=S.key_name||'';
    $('keyName').oninput=mirror;
  }
  if($('themeWallpaper')){
    $('themeWallpaper').value=S.wallpaper!==undefined?S.wallpaper:0;
    $('themeWallpaper').onchange=()=>sendThemePreview(true);
  }
  initPickerEvents();
  renderTabs();renderList();renderLook();renderNets();populatePolicy();renderSd()}
const sel=()=>S?S.coins.filter(c=>c[6]):[];
function mirror(){const s=sel();document.documentElement.style.setProperty('--home','#'+col);
  $('led').className='led'+([1,3,4,6].includes(fx)?' breathe':[2,9].includes(fx)?' rainbow':'');
  $('scrOwner').textContent=($('keyName')&&$('keyName').value.trim())||(S&&S.key_name)||'T-KEY';
  $('scrCount').textContent=s.length?s.length+' COINS':'';
  $('scrChips').innerHTML=s.slice(0,18).map(c=>`<i style="background:#${vis(c[5])}"></i>`).join('')}
function tally(){const s=sel(),n=new Set(s.map(c=>c[3])).size;
  $('tally').innerHTML=s.length?`<b>${s.length}</b> ${s.length==1?'coin':'coins'} on <b>${n}</b> ${n==1?'address':'addresses'}`:'No coins on yet. Turn on at least one.';mirror()}
function shown(){const q=$('q').value.trim().toLowerCase();
  return S.coins.filter(c=>(tab==='all'||(tab==='sel'?c[6]:c[4]==tab))&&(!q||(c[0]+' '+c[1]+' '+c[2]).toLowerCase().includes(q)))}
function renderTabs(){const t=[['all','All'],[0,'Crypto'],[1,'Stablecoins'],[2,'Meme'],['sel','On']];
  $('tabs').innerHTML=t.map(([k,l])=>`<button data-t="${k}" aria-pressed="${k===tab}">${l}</button>`).join('');
  $('tabs').querySelectorAll('button').forEach(e=>e.onclick=()=>{const v=e.dataset.t;tab=isNaN(v)?v:+v;renderTabs();renderList()})}
function groupLine(f){const n=S.families[f];return f==1?'These share your Ethereum address, on each coin\'s own network':f==2?'These share your Solana address':f==5?'These share your TRON address':'Uses your '+n+' address'}
function vis(h){const n=parseInt(h,16),r=n>>16,g=n>>8&255,b=n&255,l=(.2126*r+.7152*g+.0722*b)/255;if(l>=.28)return h;const k=(.28-l)/(1-l);
  return[r,g,b].map(v=>Math.round(v+(255-v)*k).toString(16).padStart(2,'0')).join('')}
function renderList(){const by={};shown().forEach(c=>(by[c[3]]=by[c[3]]||[]).push(c));
  const html=Object.keys(by).sort((a,b)=>a-b).map(f=>`<div class="group"><p>${esc(groupLine(+f))}</p>`+by[f].map(c=>
   `<label class="coin${c[6]?' on':''}" style="--c:#${vis(c[5])}"><span class="chip"></span><span class="who"><b>${esc(c[0])}</b><span>${esc(c[1])}</span></span>`+
   `<span class="${c[4]==1?'kind stable':c[4]==2?'kind meme':'net'}">${c[4]==1?'Stablecoin':c[4]==2?'Meme':''}</span><span class="net">${c[2]===c[1]?'':esc(c[2])}</span>`+
   `<span class="sw"><input type="checkbox" data-s="${esc(c[0])}" aria-label="${esc(c[1])}"${c[6]?' checked':''}><span></span></span></label>`).join('')+'</div>').join('');
  $('list').innerHTML=html||'<p class="note" style="margin-top:14px">No coin matches that search. Try a symbol like BTC or a network like Solana.</p>';
  $('list').querySelectorAll('input').forEach(e=>e.onchange=()=>{S.coins.find(c=>c[0]===e.dataset.s)[6]=e.checked?1:0;e.closest('.coin').classList.toggle('on',e.checked);tally()});tally()}
$('q').oninput=()=>renderList();
$('selVis').onclick=()=>{shown().forEach(c=>c[6]=1);renderList()};
$('clrVis').onclick=()=>{shown().forEach(c=>c[6]=0);renderList()};
function updatePickerUI(commit){
  const [r,g,b]=hsvToRgb(curH,curS,curV);
  const hex=rgbToHex(r,g,b);
  $('satValBox').style.backgroundColor=`hsl(${curH},100%,50%)`;
  $('svHandle').style.left=(curS*100)+'%';
  $('svHandle').style.top=((1-curV)*100)+'%';
  $('hueHandle').style.left=((curH/360)*100)+'%';
  $('pickPreview').style.backgroundColor='#'+hex;
  $('colHex').textContent='#'+hex;
  col=hex;
  mirror();
  sendThemePreview(commit);
  if(commit){renderLook()}
}

function initPickerEvents(){
  const sv=$('satValBox'),hb=$('hueBox');
  function onSv(e,commit){
    const r=sv.getBoundingClientRect();
    curS=Math.max(0,Math.min(1,(e.clientX-r.left)/r.width));
    curV=Math.max(0,Math.min(1,1-(e.clientY-r.top)/r.height));
    updatePickerUI(commit);
  }
  sv.onpointerdown=e=>{sv.setPointerCapture(e.pointerId);onSv(e,false)};
  sv.onpointermove=e=>{if(e.buttons>0||e.pointerType==='touch')onSv(e,false)};
  sv.onpointerup=e=>{onSv(e,true)};

  function onHb(e,commit){
    const r=hb.getBoundingClientRect();
    curH=Math.round(Math.max(0,Math.min(1,(e.clientX-r.left)/r.width))*360)%360;
    updatePickerUI(commit);
  }
  hb.onpointerdown=e=>{hb.setPointerCapture(e.pointerId);onHb(e,false)};
  hb.onpointermove=e=>{if(e.buttons>0||e.pointerType==='touch')onHb(e,false)};
  hb.onpointerup=e=>{onHb(e,true)};

  $('btnNewCm').onclick=()=>{
    cmTempColors=[col];
    $('cmName').value='Mode '+(customModes.length+1);
    $('cmEditor').classList.remove('hide');
    renderCmSlots();
  };
  $('btnCancelCm').onclick=()=>{$('cmEditor').classList.add('hide')};
  $('btnAddColor').onclick=()=>{
    if(cmTempColors.length<4){cmTempColors.push(col);renderCmSlots()}
  };
  $('btnSaveCm').onclick=()=>{
    const name=$('cmName').value.trim()||('Mode '+(customModes.length+1));
    if(customModes.length>=4)return;
    customModes.push({name:name.slice(0,15),fx,speed:spd,colors:[...cmTempColors]});
    activeCustom=customModes.length-1;
    $('cmEditor').classList.add('hide');
    renderLook();sendThemePreview(true);
  };
}

function renderCmSlots(){
  $('cmSlots').innerHTML=cmTempColors.map((c,i)=>`
    <div style="position:relative;display:inline-block">
      <span style="display:inline-block;width:28px;height:28px;border-radius:50%;background:#${c};border:2px solid #fff"></span>
      ${cmTempColors.length>1?`<button type="button" data-rmc="${i}" style="position:absolute;top:-4px;right:-4px;width:16px;height:16px;border-radius:50%;background:#ff3b30;color:#fff;border:none;padding:0;font-size:10px;line-height:16px;cursor:pointer">✕</button>`:''}
    </div>`).join('');
  $('cmSlots').querySelectorAll('[data-rmc]').forEach(b=>b.onclick=()=>{
    cmTempColors.splice(+b.dataset.rmc,1);renderCmSlots();
  });
  $('btnAddColor').classList.toggle('hide',cmTempColors.length>=4);
}

function renderLook(){
  const [r,g,b]=hexToRgb(col);
  const [h,s,v]=rgbToHsv(r,g,b);
  curH=h;curS=s;curV=v;
  $('colHex').textContent='#'+col;
  $('pickPreview').style.backgroundColor='#'+col;
  $('satValBox').style.backgroundColor=`hsl(${curH},100%,50%)`;
  $('svHandle').style.left=(curS*100)+'%';
  $('svHandle').style.top=((1-curV)*100)+'%';
  $('hueHandle').style.left=((curH/360)*100)+'%';

  $('sw').innerHTML=PRESETS.map(p=>`<button class="swatch" style="--s:#${p}" data-c="${p}" aria-label="#${p}" aria-pressed="${p===col&&activeCustom<0}"></button>`).join('');
  $('sw').querySelectorAll('[data-c]').forEach(b=>b.onclick=()=>{
    col=b.dataset.c;activeCustom=-1;
    const [cr,cg,cb]=hexToRgb(col);
    const [ch,cs,cv]=rgbToHsv(cr,cg,cb);
    curH=ch;curS=cs;curV=cv;
    renderLook();sendThemePreview(true);
  });

  $('fx').querySelectorAll('button').forEach(b=>{
    b.setAttribute('aria-pressed',+b.dataset.fx===fx);
    b.onclick=()=>{fx=+b.dataset.fx;renderLook();sendThemePreview(true)}
  });

  $('segSpeed').querySelectorAll('button').forEach(b=>{
    b.setAttribute('aria-pressed',+b.dataset.spd===spd);
    b.onclick=()=>{spd=+b.dataset.spd;renderLook();sendThemePreview(true)}
  });

  $('segBright').querySelectorAll('button').forEach(b=>{
    b.setAttribute('aria-pressed',+b.dataset.b===bright);
    b.onclick=()=>{bright=+b.dataset.b;renderLook();sendThemePreview(true)}
  });

  renderCustomModes();
  mirror();
}

function renderCustomModes(){
  $('cmCountLabel').textContent=customModes.length;
  $('btnNewCm').classList.toggle('hide',customModes.length>=4);
  const FX_NAMES=['Steady','Breathe','Rainbow','Heartbeat','Candle','Aurora','Ocean','Sparkle','Comet','Cycle'];
  $('cmList').innerHTML=customModes.map((cm,i)=>`
    <div class="cm-card${activeCustom===i?' active':''}">
      <div>
        <b style="font-size:14px">${esc(cm.name)}</b>
        <div style="font-size:12px;color:var(--muted);margin-top:2px">${FX_NAMES[cm.fx]||'FX'} · ${cm.speed===1?'0.5x':cm.speed===2?'0.75x':cm.speed===3?'1.0x':cm.speed===4?'1.5x':'2.0x'}</div>
        <div class="cm-chips" style="margin-top:4px">${cm.colors.map(c=>`<i style="background:#${c}"></i>`).join('')}</div>
      </div>
      <div style="display:flex;gap:6px">
        <button type="button" class="quiet" data-use="${i}" style="padding:5px 9px;font-size:12px">${activeCustom===i?'Active':'Use'}</button>
        <button type="button" class="quiet" data-del="${i}" style="padding:5px 9px;font-size:12px">✕</button>
      </div>
    </div>`).join('')||'<p class="note" style="margin:4px 0">No custom modes saved yet.</p>';

  $('cmList').querySelectorAll('[data-use]').forEach(b=>b.onclick=()=>{
    const idx=+b.dataset.use;
    activeCustom=idx;
    const cm=customModes[idx];
    if(cm.colors.length)col=cm.colors[0];
    fx=cm.fx;spd=cm.speed;
    renderLook();sendThemePreview(true);
  });
  $('cmList').querySelectorAll('[data-del]').forEach(b=>b.onclick=()=>{
    const idx=+b.dataset.del;
    customModes.splice(idx,1);
    if(activeCustom===idx)activeCustom=-1;
    else if(activeCustom>idx)activeCustom--;
    renderLook();
  });
}
function renderNets(){$('nets').innerHTML=S.wifi.map(s=>`<div class="net-row${delNets.has(s)?' gone':''}"><span>${esc(s)}</span><button class="link" data-d="${esc(s)}">${delNets.has(s)?'Keep':'Remove'}</button></div>`).join('')+
  newNets.map((n,i)=>`<div class="net-row"><span>${esc(n.s)}<small>new</small></span><button class="link" data-n="${i}">Undo</button></div>`).join('')||'<p class="note">No networks saved yet.</p>';
  $('nets').querySelectorAll('[data-d]').forEach(b=>b.onclick=()=>{const s=b.dataset.d;delNets.has(s)?delNets.delete(s):delNets.add(s);renderNets()});
  $('nets').querySelectorAll('[data-n]').forEach(b=>b.onclick=()=>{newNets.splice(+b.dataset.n,1);renderNets()})}
$('addNet').onclick=()=>{const s=$('ws').value.trim();if(!s){say('Type a network name first.','bad');return}
  if(newNets.length>=5){say('Add up to 5 networks at a time, then save.','bad');return}
  newNets.push({s,p:$('wp').value});$('ws').value='';$('wp').value='';say('');renderNets()};
$('scan').onclick=async()=>{$('scan').textContent='Finding…';try{const n=await api('/api/scan');const l=$('scanList');
  l.innerHTML='<option value="">Networks nearby</option>'+n.map(x=>`<option value="${esc(x.ssid)}">${esc(x.ssid)} (${x.rssi} dBm${x.secure?'':', open'})</option>`).join('');
  l.classList.remove('hide');l.onchange=()=>{if(l.value)$('ws').value=l.value}}catch(e){say(e.message,'bad')}$('scan').textContent='Find'};
$('lb').onclick=async()=>{try{await api('/api/login',{password:$('lp').value});$('lp').value='';load()}catch(e){$('lock').textContent=e.message;$('lock').classList.remove('hide');load()}};
$('lp').onkeydown=e=>{if(e.key==='Enter')$('lb').click()};
$('exit').onclick=async()=>{try{await api('/api/exit',{});say('Left without saving. You can close this page.','ok');$('exit').classList.add('hide');$('save').classList.add('hide')}catch(e){say(e.message,'bad')}};

(function tick(){const d=new Date();$('scrBig').textContent=String(d.getHours()).padStart(2,'0')+':'+String(d.getMinutes()).padStart(2,'0');setTimeout(tick,(60-d.getSeconds())*1000)})();

const decoyAvailable=false;
const ACTIONS={
  nothing:'Do nothing',
  decoy:'Open decoy wallet',
  decoy_wipe:'Decoy + erase real',
  wipe:'Erase key, keep SD backup',
  wipe_shred:'Erase key + SD backup'
};
const ACTION_HELP={
  nothing:'Nothing is erased.',
  decoy:'Shows a decoy wallet; your real wallet stays hidden.',
  decoy_wipe:'Shows a decoy wallet after silently erasing the real one.',
  wipe:'Erases the wallet, passkeys and PINs on the key. The encrypted SD backup survives, so you can restore.',
  wipe_shred:'Erases the key AND destroys the SD backup. Only your written recovery words can restore it.'
};
function policyHelp(){for(const [sel,box] of [['polDuress','hDuress'],['polPanic','hPanic'],['polLockout','hLockout']]){const v=$(sel).value;$(box).textContent=(sel==='polDuress'&&v==='nothing')?'Behaves exactly like a wrong PIN.':(sel==='polLockout'&&v==='nothing')?'The key stays locked; set a new PIN here to unlock it.':ACTION_HELP[v]||''}}
function populatePolicy(){
  if(!S||!S.policy)return;
  const p=S.policy;
  const dOpts=[
    ['nothing','Act like a wrong PIN'],
    ...(decoyAvailable?[['decoy',ACTIONS.decoy],['decoy_wipe',ACTIONS.decoy_wipe]]:[]),
    ['wipe',ACTIONS.wipe],
    ['wipe_shred',ACTIONS.wipe_shred]
  ];
  $('polDuress').innerHTML=dOpts.map(([v,l])=>`<option value="${v}"${p.duress===v?' selected':''}>${esc(l)}</option>`).join('');
  const pOpts=[
    ['nothing','Off (ignore the hold)'],
    ['wipe',ACTIONS.wipe],
    ['wipe_shred',ACTIONS.wipe_shred]
  ];
  $('polPanic').innerHTML=pOpts.map(([v,l])=>`<option value="${v}"${p.panic===v?' selected':''}>${esc(l)}</option>`).join('');
  const lOpts=[
    ['nothing','Stay locked'],
    ['wipe',ACTIONS.wipe],
    ['wipe_shred',ACTIONS.wipe_shred]
  ];
  $('polLockout').innerHTML=lOpts.map(([v,l])=>`<option value="${v}"${p.lockout===v?' selected':''}>${esc(l)}</option>`).join('');
  let cd='';
  for(let i=0;i<=10;i++){
    const lbl=i===0?'0 s (immediate)':i===3?'3 s (default)':i+' s';
    cd+=`<option value="${i}"${p.countdown===i?' selected':''}>${lbl}</option>`;
  }
  $('polCountdown').innerHTML=cd;
  for(const id of ['polDuress','polPanic','polLockout'])$(id).onchange=policyHelp;
  policyHelp();
}

function renderSd(){
  if(!S||!S.sd)return;
  const sd=S.sd;
  if(!sd.mounted){
    $('sdStatus').textContent='No MicroSD card detected. Insert a card and reload.';
    $('sdActions').classList.add('hide');
  }else{
    $('sdStatus').textContent='MicroSD card detected and ready.';
    $('sdActions').classList.remove('hide');

    // Full backup controls
    $('btnSdFullBackup').disabled=!S.hasPassword;
    if(sd.hasFullBackup){
      $('sdFullStatus').innerHTML='Full backup found: <code>/vault/full.tkb</code>';
      $('btnSdFullRestore').classList.remove('hide');
    }else{
      $('sdFullStatus').textContent='No full backup found on card.';
      $('btnSdFullRestore').classList.add('hide');
    }
    $('sdFullRestWarnGroup').classList.toggle('hide',!S.hasSeed&&!S.provisioned);
    if($('sdFullRestConfirm'))$('sdFullRestConfirm').checked=false;

    // Disaster copy controls
    $('btnSdBackup').disabled=!S.hasSeed;
    if(sd.hasBackup){
      $('sdSeedStatus').innerHTML='Disaster copy found: <code>/vault/tkey_backup.vault</code>';
      $('btnSdRestore').classList.remove('hide');
    }else{
      $('sdSeedStatus').textContent='No disaster copy found on card.';
      $('btnSdRestore').classList.add('hide');
    }
    $('sdRestorePinGroup').classList.toggle('hide',!S.hasSeed);
    $('sdReplaceWarnGroup').classList.toggle('hide',!S.hasSeed);
    if($('sdReplaceConfirm'))$('sdReplaceConfirm').checked=false;
  }
}

$('btnSdFullBackup').onclick=()=>{
  $('sdFullBackupBox').classList.toggle('hide');
  $('sdFullRestoreBox').classList.add('hide');
};
$('cancelSdFullBackup').onclick=()=>$('sdFullBackupBox').classList.add('hide');
$('doSdFullBackup').onclick=async()=>{
  const pw=$('sdFullPw').value;
  if(!pw){say('Enter your current setup password.','bad');return}
  $('doSdFullBackup').disabled=true;
  say('Writing full encrypted backup to MicroSD (~14 s)…');
  try{
    await api('/api/sd_full_backup',{setup_pass:pw});
    $('sdFullPw').value='';
    $('sdFullBackupBox').classList.add('hide');
    say('Full device backup saved to /vault/full.tkb.','ok');
    load();
  }catch(e){
    say(e.message,'bad');
  }finally{
    $('doSdFullBackup').disabled=false;
  }
};

$('btnSdFullRestore').onclick=()=>{
  $('sdFullRestoreBox').classList.toggle('hide');
  $('sdFullBackupBox').classList.add('hide');
};
$('cancelSdFullRestore').onclick=()=>$('sdFullRestoreBox').classList.add('hide');
$('doSdFullRestore').onclick=async()=>{
  if((S.hasSeed||S.provisioned)&&$('sdFullRestConfirm')&&!$('sdFullRestConfirm').checked){
    say('Confirm settings replacement before restoring.','bad');return;
  }
  const pw=$('sdFullRestPw').value;
  if(!pw){say('Enter the backup setup password.','bad');return}
  $('doSdFullRestore').disabled=true;
  say('Decrypting and restoring full backup…');
  try{
    await api('/api/sd_full_restore',{setup_pass:pw});
    $('sdFullRestPw').value='';
    $('sdFullRestoreBox').classList.add('hide');
    say('Restore successful! Key is restarting now…','ok');
    $('bar').classList.add('hide');
    $('app').classList.add('hide');
  }catch(e){
    say(e.message,'bad');
    $('doSdFullRestore').disabled=false;
  }
};

$('btnSdEraseToggle').onclick=()=>{
  $('sdEraseBox').classList.toggle('hide');
};
$('cancelSdErase').onclick=()=>$('sdEraseBox').classList.add('hide');
$('doSdErase').onclick=async()=>{
  const cf=$('sdEraseConfirm').value.trim();
  if(cf!=='ERASE'){say('Type ERASE in uppercase to confirm.','bad');return}
  $('doSdErase').disabled=true;
  say('Securely erasing vault and PSBT files…');
  try{
    await api('/api/sd_erase',{confirm:cf});
    $('sdEraseConfirm').value='';
    $('sdEraseBox').classList.add('hide');
    say('MicroSD vault erased.','ok');
    load();
  }catch(e){
    say(e.message,'bad');
  }finally{
    $('doSdErase').disabled=false;
  }
};

$('btnSdBackup').onclick=()=>{
  $('sdBackupBox').classList.toggle('hide');
  $('sdRestoreBox').classList.add('hide');
};
$('cancelSdBackup').onclick=()=>$('sdBackupBox').classList.add('hide');
$('doSdBackup').onclick=async()=>{
  const pin=$('sdPin').value,pass=$('sdPass').value,pass2=$('sdPass2').value;
  if(!pin){say('Enter your current PIN.','bad');return}
  if(pass!==pass2){say('Passphrases do not match.','bad');return}
  if(pass.length<12){say('Passphrase must be at least 12 characters.','bad');return}
  $('doSdBackup').disabled=true;
  say('Writing encrypted backup to MicroSD…');
  try{
    await api('/api/sd_backup',{pin,passphrase:pass,passphrase_confirm:pass2});
    $('sdPin').value='';$('sdPass').value='';$('sdPass2').value='';
    $('sdBackupBox').classList.add('hide');
    say('Encrypted backup saved to MicroSD.','ok');
    load();
  }catch(e){
    say(e.message,'bad');
  }finally{
    $('doSdBackup').disabled=false;
  }
};

$('btnSdRestore').onclick=()=>{
  $('sdRestoreBox').classList.toggle('hide');
  $('sdBackupBox').classList.add('hide');
};
$('cancelSdRestore').onclick=()=>$('sdRestoreBox').classList.add('hide');
$('doSdRestore').onclick=async()=>{
  const pin=$('sdRestPin').value,pass=$('sdRestPass').value;
  if(S.hasSeed){
    if(!pin){say('Enter your current PIN to authorize restore.','bad');return}
    if(!$('sdReplaceConfirm').checked){say('Confirm wallet replacement before restoring.','bad');return}
  }
  if(!pass){say('Enter the backup passphrase.','bad');return}
  $('doSdRestore').disabled=true;
  say('Decrypting and restoring wallet…');
  try{
    const b={passphrase:pass};
    if(S.hasSeed){b.pin=pin;b.replace='1'}
    await api('/api/sd_restore',b);
    $('sdRestPin').value='';$('sdRestPass').value='';
    if($('sdReplaceConfirm'))$('sdReplaceConfirm').checked=false;
    $('sdRestoreBox').classList.add('hide');
    say('Wallet restored from MicroSD backup.','ok');
    load();
  }catch(e){
    say(e.message,'bad');
  }finally{
    $('doSdRestore').disabled=false;
  }
};

$('save').onclick=async()=>{
  const pin=$('pin').value,sp=$('sp').value;
  if(pin!==$('pin2').value){say('The two PINs don\'t match.','bad');return}
  if(sp!==$('sp2').value){say('The two setup passwords don\'t match.','bad');return}
  if(!sel().length){say('Turn on at least one coin.','bad');return}
  const kn=$('keyName')?$('keyName').value.trim():'';
  const wp=$('themeWallpaper')?$('themeWallpaper').value:'0';
  const b={pin,duress:$('du').value,duress_clear:$('duClear').checked?'1':'',setup_pass:sp,
    pol_duress:$('polDuress').value,pol_panic:$('polPanic').value,
    pol_lockout:$('polLockout').value,pol_countdown:$('polCountdown').value,
    key_name:kn,theme_wallpaper:wp,
    coins:sel().map(c=>c[0]).join(','),
    theme_rgb:col,theme_fx:String(fx),theme_speed:String(spd),theme_bright:String(bright),
    theme_custom_idx:String(activeCustom),cm_count:String(customModes.length)};
  customModes.forEach((cm,i)=>{
    b['cm_'+i+'_name']=cm.name;
    b['cm_'+i+'_fx']=String(cm.fx);
    b['cm_'+i+'_speed']=String(cm.speed);
    b['cm_'+i+'_colors']=cm.colors.join(',');
  });
  newNets.forEach((n,i)=>{b['ws'+i]=n.s;b['wp'+i]=n.p});[...delNets].forEach((s,i)=>b['wd'+i]=s);
  $('save').disabled=true;say('Saving to the key…');
  try{await api('/api/save',b);['pin','pin2','du','sp','sp2'].forEach(i=>$(i).value='');
    say('Saved to the key. It turns its Wi-Fi off now, so you can close this page.','ok');$('exit').classList.add('hide');$('save').classList.add('hide')}
  catch(e){say(e.message,'bad');$('save').disabled=false}};
load();
</script></body></html>)PAGE";
