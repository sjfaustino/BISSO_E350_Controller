# Developer Reference (R3)
- `i2c_lock.*` provides cooperative lock to serialize LCD and IO expander traffic.
- `io.*` keeps shadow output bytes; `pushOutputs()` flushes once per loop.
- `motion.*` asserts speed/axis/dir then `Y_VS`; soft-limits and stall detection.
- `wj66.*` parses comma protocol: header + X,Y,Z,A signed 6-digit fields.
- `config.*` validates and persists; JSON import/export includes `cal[]`.
- `journal.*` buffered writes + rotation; tail streaming avoids big allocations.
- `inputs.*` debounced buttons; HB LED.
