# MLK File Records

MLK keys identify individual embedded resources, while filenames group those records into broader battle-resource packages. The following families are useful navigation aids rather than guaranteed semantic contracts.

| Filename family | General role |
| --- | --- |
| `bchara/cr###.mlk` | Sequential crew-character battle animation packages. |
| `bchara/cren##.mlk` | A related but distinct crew or enemy resource group. |
| `bchara/jouchu.mlk` | Large shared or resident battle animation/effect bundle. |
| `beff/d24*.mlk` | Battle command or special-move effect packages, sometimes split across suffix variants. |
| `beff/d05*.mlk` | Small shared helper-effect packages. |
| `beff/d29*.mlk` | Separate command/effect family with some anomalous members. |
| `beff/f*.mlk` | Lower-level effect or per-context component packages; several subfamilies reuse suffix patterns. |
| `pcp##.mlk` | Compact party-composition-related effect packages; exact presentation role remains provisional. |
| `pcwin.mlk` | Larger battle-state or party-window-adjacent resource package; exact role remains provisional. |

Most records contain MLD-like payloads. Empty, compressed, Ninja, `POF0`, and unknown payloads are valid classifications and should not be forced into the MLD model.
