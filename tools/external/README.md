# External Tools

The game-data pipeline uses two tools from other projects that only work for us
with changes we made. We do not maintain forks of them. Instead, each folder
here holds our changes as patch files against a pinned upstream commit, plus the
small scripts we run alongside them, so anyone can rebuild the exact versions we
use.

| Tool | Used for | Upstream | Base commit | License |
|------|----------|----------|-------------|---------|
| [wow.export](wow.export/) | Creature models, dungeon geometry for encounter maps, the Blender importer | [Kruithne/wow.export](https://github.com/Kruithne/wow.export) | `c2fd7bd` (0.2.19) | MIT |
| [db2reader](db2reader/) | Converting raw `.db2` files to the text exports every data script reads | [erorus/db2](https://github.com/erorus/db2) | `1dcc652` | Apache-2.0 |

These are only needed to regenerate game data for a new patch. Building and
running AwowLab from source does not need any of them; the generated data is
already committed under `AwowLab/data/`.

## Applying patches

Every patch series is applied with `git am` onto a clean clone at the base
commit:

```bash
git clone <upstream> && cd <repo>
git config core.autocrlf false
git checkout <base commit>
git am --keep-cr /path/to/AwowLab/tools/external/<tool>/patches/*.patch
```

Both settings matter. wow.export stores some files with CRLF line endings, and
`git am` strips carriage returns from patches unless told not to, so without
`--keep-cr` the first patch fails to apply. `core.autocrlf false` keeps the
checkout byte-identical to what the patches were made from.

The patches in each folder were checked by applying them to a fresh clone and
comparing the result with the tree we actually build from.

Patches against a tool keep that tool's license.

rustydemon, used to extract world map tiles, is not here: stock releases from
<https://github.com/HoldMyBeer-gg/rustydemon> do everything we use.

## When upstream moves

The patches are pinned to the base commit above and are not rebased as upstream
changes. Building at the base commit always works. If you need a newer upstream,
expect to resolve conflicts by hand.
