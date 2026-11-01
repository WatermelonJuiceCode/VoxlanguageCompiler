# Minimal Caching

In **Vox v1.6.0**, a caching feature was added, greatly improving compilation speed!
If the current code is identical to the previously executed code, the existing `.exe` file is run directly.
If the current code is identical to previously executed code, the previous `.exe` is executed directly.

If you want to recompile from scratch, simply run the `vox clean` command to clear the cache.

If your Vox project is ready for release, use `vox run "file.v" -o` or `vox run "file.v" --op` (the two are equivalent) to optimize your code. This will greatly boost performance, but compilation time will also multiply — use it as appropriate.