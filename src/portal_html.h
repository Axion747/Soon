#pragma once
#include <pgmspace.h>

// The entire setup / settings web page, served both from the captive portal
// (setup mode) and at http://soon.local once the device is online.
static const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Soon · setup</title>
<style>
  :root { --bg:#0f0f14; --card:#1b1b26; --line:#2a2a3a; --text:#f0f0f5;
          --dim:#9a9aac; --accent:#ff5e8c; --ok:#6fdc96; --bad:#ff7b6b; }
  * { box-sizing:border-box; margin:0; padding:0; }
  body { background:var(--bg); color:var(--text); min-height:100vh;
         font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
         display:flex; justify-content:center; padding:20px 14px 40px; }
  .wrap { width:100%; max-width:430px; }
  h1 { font-size:28px; margin:8px 0 2px; }
  h1 .heart { color:var(--accent); }
  .sub { color:var(--dim); font-size:14px; margin-bottom:18px; }
  .card { background:var(--card); border:1px solid var(--line); border-radius:16px;
          padding:16px; margin-bottom:14px; }
  .card h2 { font-size:15px; margin-bottom:12px; color:var(--dim);
             text-transform:uppercase; letter-spacing:.06em; }
  label { display:block; font-size:13px; color:var(--dim); margin:10px 0 4px; }
  input, select { width:100%; padding:12px; border-radius:10px; border:1px solid var(--line);
          background:#12121a; color:var(--text); font-size:16px; }
  input:focus, select:focus { outline:2px solid var(--accent); border-color:transparent; }
  button { width:100%; padding:14px; margin-top:14px; border:0; border-radius:12px;
           background:var(--accent); color:#fff; font-size:16px; font-weight:600; }
  button.ghost { background:transparent; border:1px solid var(--line); color:var(--dim);
                 font-weight:500; }
  button:disabled { opacity:.5; }
  .net { display:flex; justify-content:space-between; align-items:center;
         padding:12px 10px; border-bottom:1px solid var(--line); font-size:15px; }
  .net:last-child { border-bottom:0; }
  .net:hover, .net.sel { background:#232333; border-radius:10px; cursor:pointer; }
  .net .bars { color:var(--dim); font-size:13px; }
  .pill { display:inline-block; padding:4px 10px; border-radius:999px; font-size:12px;
          border:1px solid var(--line); color:var(--dim); }
  .pill.ok { color:var(--ok); border-color:var(--ok); }
  .pill.bad { color:var(--bad); border-color:var(--bad); }
  .status { text-align:center; padding:10px 0 2px; font-size:15px; color:var(--dim); }
  .big-ok { text-align:center; font-size:44px; padding:8px 0 0; }
  .hidden { display:none; }
  .spin { display:inline-block; width:14px; height:14px; border:2px solid var(--dim);
          border-top-color:var(--accent); border-radius:50%;
          animation:sp 0.8s linear infinite; vertical-align:-2px; }
  @keyframes sp { to { transform:rotate(360deg); } }
  .row { display:flex; gap:10px; }
  .row > * { flex:1; }
  .foot { text-align:center; color:var(--dim); font-size:12px; margin-top:18px; }
</style>
</head>
<body>
<div class="wrap">
  <h1>Soon <span class="heart">&#10022;</span></h1>
  <div class="sub" id="subtitle">a little countdown, just for you</div>

  <!-- ===== WiFi setup (shown in setup mode) ===== -->
  <div class="card hidden" id="wifiCard">
    <h2>Connect me to your WiFi</h2>
    <div id="netList"><div class="status"><span class="spin"></span> &nbsp;looking for networks&hellip;</div></div>
    <div id="joinBox" class="hidden">
      <label id="passLabel">Password for <b id="chosenSsid"></b></label>
      <input type="password" id="pass" placeholder="WiFi password" autocomplete="off">
    </div>
    <button id="rescan" class="ghost" type="button">Scan again</button>
  </div>

  <!-- ===== Connection progress ===== -->
  <div class="card hidden" id="connCard">
    <div class="big-ok" id="connIcon"></div>
    <div class="status" id="connMsg"></div>
  </div>

  <!-- ===== Countdown settings (always shown) ===== -->
  <div class="card" id="cdCard">
    <h2>The countdown &amp; her message</h2>
    <label>Message on her second page</label>
    <input id="msg" maxlength="60" placeholder="i love you so much">
    <div class="row">
      <div>
        <label>The big day</label>
        <input type="date" id="date">
      </div>
      <div>
        <label>Timezone</label>
        <select id="tz"></select>
      </div>
    </div>
  </div>

  <!-- ===== Device clock (works with or without internet) ===== -->
  <div class="card" id="clockCard">
    <h2>Device clock</h2>
    <div class="status" id="clkMsg"></div>
    <button id="clkBtn" class="ghost" type="button">Set clock from this phone</button>
  </div>

  <button id="saveBtn" type="button">Save</button>

  <!-- ===== Online-only tools ===== -->
  <div class="card hidden" id="toolsCard" style="margin-top:14px">
    <h2>Device</h2>
    <div class="status" id="updMsg"></div>
    <button id="updBtn" class="ghost" type="button">Check for updates now</button>
    <button id="forgetBtn" class="ghost" type="button">Forget WiFi (re-run setup)</button>
  </div>

  <div class="foot" id="foot"></div>
</div>

<script>
var st = null, chosen = null, scanTimer = null, pollTimer = null;

function $(id){ return document.getElementById(id); }
function post(u, data){
  return fetch(u, {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'},
                   body:new URLSearchParams(data||{}).toString()});
}
function bars(rssi){ return rssi > -55 ? '&#9679;&#9679;&#9679;' : rssi > -70 ? '&#9679;&#9679;&#9675;' : '&#9679;&#9675;&#9675;'; }

function loadStatus(){
  return fetch('/api/status').then(function(r){ return r.json(); }).then(function(s){
    st = s;
    $('msg').value = $('msg').value || s.msg;
    $('date').value = $('date').value || s.date;
    if (!$('tz').options.length) {
      s.tzs.forEach(function(t){
        var o = document.createElement('option');
        o.value = t.k; o.textContent = t.label;
        $('tz').appendChild(o);
      });
      $('tz').value = s.tz;
    }
    $('foot').textContent = 'Soon v' + s.version + ' ' + (s.build || '') + ' · ' + s.board;
    $('clkMsg').innerHTML = 'Device shows: <b>' + s.time + '</b><br>' +
      (s.timeSynced ? 'synced from the internet' : 'running on its own clock');
    if (s.mode === 'ap') {
      $('wifiCard').classList.remove('hidden');
      $('toolsCard').classList.add('hidden');
      $('subtitle').textContent = "let's get this little thing online";
    } else {
      $('wifiCard').classList.add('hidden');
      $('toolsCard').classList.remove('hidden');
      $('subtitle').textContent = 'connected to ' + s.ssid + ' · ' + s.ip;
      updMsg(s);
    }
    return s;
  });
}

function updMsg(s){
  var m = {idle:'', checking:'Checking for updates&hellip;',
           none:'You have the newest version (v' + s.version + ')',
           found:'Update v' + s.updateVer + ' found &mdash; installing!',
           updating:'Installing update&hellip; watch the screen!',
           error:'Update check failed &mdash; will retry later'};
  $('updMsg').innerHTML = m[s.update] || '';
}

function scan(fresh){
  $('netList').innerHTML = '<div class="status"><span class="spin"></span> &nbsp;looking for networks&hellip;</div>';
  fetch('/api/scan' + (fresh ? '?fresh=1' : '')).then(function(r){ return r.json(); }).then(function(d){
    if (d.status === 'scanning') { scanTimer = setTimeout(function(){ scan(false); }, 1500); return; }
    var el = $('netList'); el.innerHTML = '';
    if (!d.networks || !d.networks.length) {
      el.innerHTML = '<div class="status">No networks found &mdash; try again?</div>';
      return;
    }
    d.networks.forEach(function(n){
      var div = document.createElement('div');
      div.className = 'net';
      div.innerHTML = '<span>' + (n.open ? '' : '&#128274; ') + n.ssid + '</span><span class="bars">' + bars(n.rssi) + '</span>';
      div.onclick = function(){
        chosen = n;
        Array.prototype.forEach.call(document.querySelectorAll('.net'), function(x){ x.classList.remove('sel'); });
        div.classList.add('sel');
        $('joinBox').classList.toggle('hidden', n.open);
        $('chosenSsid').textContent = n.ssid;
        $('pass').focus();
      };
      el.appendChild(div);
    });
  }).catch(function(){ scanTimer = setTimeout(function(){ scan(false); }, 2000); });
}

function save(){
  var data = { msg: $('msg').value, date: $('date').value, tz: $('tz').value };
  $('saveBtn').disabled = true;
  if (st && st.mode === 'ap') {
    if (!chosen) { alert('Tap your WiFi network in the list first!'); $('saveBtn').disabled = false; return; }
    data.ssid = chosen.ssid;
    data.pass = $('pass').value;
    post('/api/save', data).then(function(){
      $('wifiCard').classList.add('hidden');
      $('connCard').classList.remove('hidden');
      $('connIcon').innerHTML = '<span class="spin"></span>';
      $('connMsg').textContent = 'Connecting to ' + chosen.ssid + '…';
      pollConn();
    });
  } else {
    post('/api/settings', data).then(function(){
      $('saveBtn').textContent = 'Saved!';
      setTimeout(function(){ $('saveBtn').textContent = 'Save'; $('saveBtn').disabled = false; }, 1500);
    });
  }
}

function pollConn(){
  pollTimer = setTimeout(function(){
    fetch('/api/status').then(function(r){ return r.json(); }).then(function(s){
      if (s.connstate === 'ok') {
        $('connIcon').textContent = '🎉';
        $('connMsg').innerHTML = 'Connected! All set &mdash; you can close this page.<br>The countdown is on the screen.';
      } else if (s.connstate === 'fail') {
        $('connIcon').textContent = '😕';
        $('connMsg').textContent = "That didn't work — maybe a typo in the password? Let's try again.";
        $('saveBtn').disabled = false;
        $('connCard').classList.add('hidden');
        $('wifiCard').classList.remove('hidden');
        scan();
      } else { pollConn(); }
    }).catch(pollConn);
  }, 1200);
}

$('saveBtn').onclick = save;
$('rescan').onclick = function(){ scan(true); };
$('updBtn').onclick = function(){
  post('/api/checkupdate').then(function(){
    $('updMsg').innerHTML = 'Checking for updates&hellip;';
    setTimeout(function(){ loadStatus(); }, 4000);
  });
};
$('clkBtn').onclick = function(){
  post('/api/time', { epoch: Math.floor(Date.now() / 1000) }).then(function(){
    $('clkMsg').innerHTML = 'Clock set! &#10003;';
    setTimeout(loadStatus, 1200);
  });
};
$('forgetBtn').onclick = function(){
  if (confirm('Forget the saved WiFi and go back to setup mode?')) {
    post('/api/forget');
    $('subtitle').textContent = 'restarting into setup mode…';
  }
};

loadStatus().then(function(s){ if (s.mode === 'ap') scan(); });
</script>
</body>
</html>
)HTML";
