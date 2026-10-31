# ImPlot (vendored)

Plotting widgets for the benchmark report card. Vendored rather than fetched so
the build has no extra network dependency and so local patches (if any become
necessary) stay visible in this repo's history.

| | |
|---|---|
| Upstream | https://github.com/epezent/implot |
| Commit | `d65a2bef53d32502407de3a4be80f191e2f412d7` (2026-06-01) |
| Version | 1.1 WIP (`IMPLOT_VERSION_NUM` 10100) |
| License | MIT - see `LICENSE`, retained verbatim |
| Vendored | 2026-08-03 |

Tracking master rather than the v0.16 tag: v0.16 predates ImGui v1.91.5 and does
not carry the version guards for it. Master does.

Files taken: `implot.h`, `implot.cpp`, `implot_internal.h`, `implot_items.cpp`,
`LICENSE`. The demo (`implot_demo.cpp`) is deliberately left out - it is a large
translation unit that ships nothing to users.

Local modifications: none. Keep it that way if possible; if a patch becomes
unavoidable, note it here with the reason so the next update does not silently
drop it.

## Updating

Copy the five files from a fresh clone and update the commit hash above. Check
`IMGUI_VERSION_NUM` guards still cover the ImGui tag pinned in
`AwowLab/UIManager/CMakeLists.txt`.

## Attribution

MIT requires the copyright notice to travel with binary distributions. `LICENSE`
is included in the release package for this reason - see the third-party notices
step in the release workflow.
