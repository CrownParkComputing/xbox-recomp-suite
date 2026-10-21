# Shared Chihiro runtime patches

The VCop3, HOTD3 and OutRun2 ports use these patches in the order recorded by
`tools/prepare.py`. They derive an ignored SDK copy under `.local/sdks/` and do
not alter the shared retail Xbox SDK. The patches were validated against suite
commit `9f957d6584a3e8f81d7410bcbd79ed342e52cc51` for the standalone native ports.

- `chihiro-memory.patch`: 128 MiB physical memory.
- `flush-guest-push.patch`: deliver completed visibility reports before Present.
- `buffer-envelope.patch`: pitch conversion and an explicit unsupported envelope result.
- `indexed-up.patch`: indexed draws from user memory.
- `controller-capabilities.patch`: packed XInput capability reports.
- `target-alpha.patch`: sample XRGB render targets with opaque alpha.

Existing per-title XDK layout evidence and cabinet hooks remain in the ports.
The renderer patches include focused test changes; runtime pause/clock behavior
continues to come from the shared suite SDK.
