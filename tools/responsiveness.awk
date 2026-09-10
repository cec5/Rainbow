# Summarizes Rainbow debug logs: how each run went, and how responsive it was.
#
# Runs:       duration, cycles, how the run ended, error count.
# Timer:      requested vs actual wait on evnt_multi MU_TIMER returns (requested > 0).
# Turnaround: time from evnt_multi/evnt_mesag handing the guest an event until it waits again.
# Sampling:   interval between consecutive graf_mkstate polls (gaps <= 100 ms, i.e. during a drag).
#
# Timestamps come from the log itself, so figures include logging overhead and share the
# resolution of the Windows clock (about 15.6 ms by default).

function t(s,  a,b){ gsub(/[\[\]]/,"",s); split(s,a,":"); split(a[3],b,"."); return a[1]*3600000+a[2]*60000+b[1]*1000+b[2] }
function pct(arr, n, p,  i){ i = int(n*p + 0.5); if (i < 1) i = 1; if (i > n) i = n; return arr[i] }
function stats(label, src, n,  v, i, s){
  if (!n) return
  for (i = 1; i <= n; i++) v[i] = src[i]
  asort(v); s = 0; for (i = 1; i <= n; i++) s += v[i]
  printf "  %-22s n=%-5d median=%dms  p90=%dms  max=%dms  mean=%.1fms\n", label, n, pct(v, n, 0.5), pct(v, n, 0.9), v[n], s/n
}

FNR == 1 { runs[++nr] = FILENAME; pend = 0; have = 0; lastmk = -1 }
/^\[[0-9]+\] \[[0-9:.]+\]/ { ts_now = t($2); if (!(FILENAME in first)) first[FILENAME] = ts_now; last[FILENAME] = ts_now }
/\[ERROR\]/ { errs[FILENAME]++ }
/Execution halted after/ { match($0, /after [0-9]+ cycles/); cyc[FILENAME] = substr($0, RSTART+6, RLENGTH-13) }
/screen window closed/        { how[FILENAME] = "window closed" }
/Pterm -> halt requested/     { how[FILENAME] = "Pterm" }
/CPU FAULT/                   { how[FILENAME] = "CPU fault" }
/safety cap/                  { how[FILENAME] = "cycle cap" }

/AES graf_mkstate \(0x4F\) called/ {
  if (lastmk >= 0 && ts_now - lastmk <= 100) samp[++sn] = ts_now - lastmk
  lastmk = ts_now
}

/AES evnt_(multi|mesag) \(0x1[79]\) called/ {
  if (pend) { lat = ts_now - evts; pend = 0; if (kind == "key") tk[++nk] = lat; else if (kind == "button") tb[++nb] = lat; else tm[++nm] = lat; ta[++na] = lat }
  start = ts_now; have = 1
  next
}
/evnt_multi -> code=MU_TIMER after/ && have {
  match($0, /after [0-9]+ms/); req = substr($0, RSTART+6, RLENGTH-8) + 0
  if (req > 0) { act = ts_now - start; d = act - req; tact[++tn] = act; terr[tn] = (d < 0 ? -d : d); treq[req]++ }
  have = 0; next
}
/evnt_(multi|mesag) -> code=/ {
  have = 0
  kind = ($0 ~ /MU_KEYBD/) ? "key" : (($0 ~ /MU_BUTTON/) ? "button" : "message")
  pend = 1; evts = ts_now
}

END {
  print "RUNS"
  for (i = 1; i <= nr; i++) {
    f = runs[i]; n = f; sub(/.*[\/\\]/, "", n)
    printf "  %-34s %7.1fs  %12s cycles  ended: %-13s errors: %d\n", n, (last[f]-first[f])/1000, (f in cyc ? cyc[f] : "?"), (f in how ? how[f] : "?"), errs[f]
  }
  print "TIMER ACCURACY (evnt_multi, requested > 0)"
  if (tn) {
    s = 0; e = 0; for (i = 1; i <= tn; i++) { s += tact[i]; e += terr[i]; se[i] = terr[i] }
    asort(se); r = ""; for (q in treq) r = r q "ms x" treq[q] " "
    printf "  n=%d  requested: %s mean actual=%.1fms  mean |error|=%.1fms  p90 |error|=%dms  max |error|=%dms\n", tn, r, s/tn, e/tn, pct(se, tn, 0.9), se[tn]
  } else print "  (none)"
  print "TURNAROUND (event delivered -> guest waits again)"
  stats("key", tk, nk); stats("button", tb, nb); stats("message", tm, nm); stats("ALL", ta, na)
  if (!na) print "  (none)"
  print "INPUT SAMPLING (graf_mkstate poll interval during drags)"
  if (sn) stats("interval", samp, sn); else print "  (none)"
}