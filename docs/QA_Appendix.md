# QA Appendix

## Functional Checks
- G-code gate by AUTO input
- Single-axis enforcement per command
- Soft-limit reject (enqueue) and breach (runtime) -> ERROR
- Direction interlock dead-time observed with retry
- WJ66 stale fault after 3 consecutive timeouts
- Journal tail prints without heap spikes

## Persistence
- Run-hours saved every 10 min and on RUN exit
- JSON import/export round-trips `cal[]` and limits

## Self-Test
- Y01..Y09 sequenced at 500 ms, timeout at 30 s, safe exit
