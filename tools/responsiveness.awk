# Summarizes Rainbow debug logs: how each run went, and how responsive it was.
# Results are grouped by program, taken from each log's name (<program>_<date>_<time>.log), then pooled.
# Usage: gawk -f responsiveness.awk path_to_logs_folder/*.log
#
# Runs:       duration, cycles, log lines, how the run ended, error count.
# Timer:      requested vs actual wait on evnt_multi MU_TIMER returns (requested > 0).
# Turnaround: time from evnt_multi/evnt_mesag handing the guest an event until it waits again.
# Redraws:    WM_REDRAW turnaround, grouped by how many VDI calls the guest made while repainting.
# Sampling:   interval between consecutive graf_mkstate polls (gaps <= 100 ms, i.e. during a drag).
# Long gaps:  the AES calls made during any button or message turnaround of LONG_MS or more.
#
# Timestamps come from the log itself, so figures include logging overhead and share the
# resolution of the Windows clock (about 15.6 ms by default), which is why turnaround also
# reports the share within one tick (TICK_MS).

BEGIN { TICK_MS = 16; LONG_MS = 150; PROCINFO["sorted_in"] = "@ind_str_asc" }

function t(s,  a,b){ gsub(/[\[\]]/,"",s); split(s,a,":"); split(a[3],b,"."); return a[1]*3600000+a[2]*60000+b[1]*1000+b[2] }
function pct(arr, n, p,  i){ i = int(n*p + 0.5); if (i < 1) i = 1; if (i > n) i = n; return arr[i] }
function add(arr, cnt, key, val){ arr[key, ++cnt[key]] = val }

# Values live in src[key, 1..cnt[key]]; key is a program, or a program and kind joined by SUBSEP.
function stats(label, src, cnt, key,  n, v, i, s, w){
  n = cnt[key]; if (!n) return 0
  for (i = 1; i <= n; i++) v[i] = src[key, i]
  asort(v); s = 0; w = 0
  for (i = 1; i <= n; i++) { s += v[i]; if (v[i] <= TICK_MS) w++ }
  printf "    %-10s n=%-5d median=%dms  p90=%dms  max=%dms  mean=%.1fms  within %dms: %.1f%%\n", label, n, pct(v, n, 0.5), pct(v, n, 0.9), v[n], s/n, TICK_MS, 100*w/n
  return 1
}

function timer(key,  n, v, i, s, e, r, k, parts){
  n = tcnt[key]; if (!n) return
  for (i = 1; i <= n; i++) { s += tact[key, i]; e += terr[key, i]; v[i] = terr[key, i] }
  asort(v)
  for (k in treq) { split(k, parts, SUBSEP); if (parts[1] == key) r = r parts[2] "ms x" treq[k] " " }
  print "  timer accuracy (evnt_multi, requested > 0)"
  printf "    n=%d  requested: %s mean actual=%.1fms  mean |error|=%.1fms  p90 |error|=%dms  max |error|=%dms\n", n, r, s/n, e/n, pct(v, n, 0.9), v[n]
}

function section(key, title,  printed, k, parts, sizes, ns, i){
  print title
  timer(key)
  print "  turnaround (event delivered -> guest waits again)"
  printed  = stats("key",     turn, tc, key SUBSEP "key")
  printed += stats("button",  turn, tc, key SUBSEP "button")
  printed += stats("message", turn, tc, key SUBSEP "message")
  if (!printed) print "    (none)"
  ns = 0
  for (k in rd) { split(k, parts, SUBSEP); if (parts[1] == key) sizes[++ns] = parts[2] + 0 }
  if (ns) {
    asort(sizes)
    print "  redraws (WM_REDRAW, by VDI calls made while repainting)"
    for (i = ns; i >= 1; i--) printf "    %5d VDI calls  x%-3d max %dms\n", sizes[i], rd[key, sizes[i]], rdmax[key, sizes[i]]
  }
  if (scnt[key]) { print "  input sampling (graf_mkstate interval during drags)"; stats("interval", samp, scnt, key) }
}

FNR == 1 {
  runs[++nr] = FILENAME
  prog = FILENAME; sub(/.*[\/\\]/, "", prog); sub(/_[0-9]+_[0-9]+\.log$/, "", prog)
  progs[prog] = 1
  pend = 0; have = 0; lastmk = -1
}
{ lines[FILENAME] = FNR }
/^\[[0-9]+\] \[[0-9:.]+\]/ { ts_now = t($2); if (!(FILENAME in first)) first[FILENAME] = ts_now; last[FILENAME] = ts_now }
/\[ERROR\]/ { errs[FILENAME]++ }
/Execution halted after/ { match($0, /after [0-9]+ cycles/); cyc[FILENAME] = substr($0, RSTART+6, RLENGTH-13) }
/screen window closed/        { how[FILENAME] = "window closed" }
/Pterm -> halt requested/     { how[FILENAME] = "Pterm" }
/CPU FAULT/                   { how[FILENAME] = "CPU fault" }
/safety cap/                  { how[FILENAME] = "cycle cap" }

/AES graf_mkstate \(0x4F\) called/ {
  if (lastmk >= 0 && ts_now - lastmk <= 100) { add(samp, scnt, prog, ts_now - lastmk); add(samp, scnt, "*", ts_now - lastmk) }
  lastmk = ts_now
}

/AES evnt_(multi|mesag) \(0x1[79]\) called/ {
  if (pend) {
    lat = ts_now - evts; pend = 0
    add(turn, tc, prog SUBSEP kind, lat); add(turn, tc, "*" SUBSEP kind, lat)
    if (redraw) { rd[prog, nvdi]++; if (!((prog, nvdi) in rdmax) || lat > rdmax[prog, nvdi]) rdmax[prog, nvdi] = lat }
    if (kind != "key" && lat >= LONG_MS) longgap[gap == "" ? "(no AES calls)" : gap]++
  }
  start = ts_now; have = 1
  next
}
pend && /\[API  \] VDI .* called/ { nvdi++ }
pend && kind != "key" && /\[API  \] AES [a-z_]+ \(0x[0-9A-F]+\) called/ {
  match($0, /AES [a-z_]+/); nm = substr($0, RSTART+4, RLENGTH-4)
  if (!(nm in seen)) { seen[nm] = 1; gap = gap nm " " }
}
/evnt_multi -> code=MU_TIMER after/ {
  if (have) {
    match($0, /after [0-9]+ms/); req = substr($0, RSTART+6, RLENGTH-8) + 0
    if (req > 0) {
      act = ts_now - start; d = act - req; if (d < 0) d = -d
      i = ++tcnt[prog]; tact[prog, i] = act; terr[prog, i] = d; treq[prog, req]++
      i = ++tcnt["*"];  tact["*", i]  = act; terr["*", i]  = d; treq["*", req]++
    }
  }
  have = 0; next
}
/evnt_(multi|mesag) -> code=/ {
  have = 0
  kind = ($0 ~ /MU_KEYBD/) ? "key" : (($0 ~ /MU_BUTTON/) ? "button" : "message")
  redraw = ($0 ~ /-> code=20 /)
  pend = 1; evts = ts_now; nvdi = 0; gap = ""; delete seen
}

END {
  print "RUNS"
  for (i = 1; i <= nr; i++) {
    f = runs[i]; n = f; sub(/.*[\/\\]/, "", n)
    printf "  %-34s %7.1fs  %12s cycles  %7d lines  ended: %-13s errors: %d\n", n, (last[f]-first[f])/1000, (f in cyc ? cyc[f] : "?"), lines[f], (f in how ? how[f] : "?"), errs[f]
  }
  for (p in progs) section(p, "PROGRAM " p)
  section("*", "ALL PROGRAMS")
  printf "LONG GAPS (button or message turnaround >= %dms): AES calls made during the gap\n", LONG_MS
  printed = 0
  for (g in longgap) { printf "  %4d  %s\n", longgap[g], g; printed = 1 }
  if (!printed) print "  (none)"
}
