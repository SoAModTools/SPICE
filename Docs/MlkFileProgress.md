# MLK File Progress

## Current Support

SPICE reads little-endian raw Dreamcast and big-endian raw or AKLZ-wrapped GameCube MLK headers and `0x10`-byte record tables. Structural endian detection and a forced-endian corpus path preserve anomaly evidence while validating keys and payload spans. Embedded MLD header probes use the selected outer byte order.

The scanner recognizes the cross-platform split-table variant found in the battle-effect corpus. These files contain a complete descriptor table but only a contiguous prefix of payloads for the current platform; the remaining descriptors either repeat the available keys for the other platform or are identified by the opposite-endian count field. SPICE exposes the descriptor count, available record count, and unavailable trailing count independently and parses only the payloads that are actually present. Record keys, offsets, sizes, and `rawWord12` are retained for correlation with the embedded resources.

Observed GameCube examples include `d2403900.mlk` (24 available payloads from 82 descriptors) and four `D/F*29*` families whose available prefix is exactly half of the descriptor table. Their Dreamcast counterparts use the same split-package concept with little-endian records. This is treated as a supported read-only structural variant rather than malformed data.

## Known Limitations

MLK writing and repacking are not yet supported. Unavailable cross-platform payloads are reported, not synthesized, and the meaning of the mixed-endian count in the `d2403900` family remains structural evidence rather than a general field rule. `rawWord12` is still unresolved, and payload classification does not replace parsing by the payload’s owning project.

Filename families provide useful hints about battle-resource grouping, but several roles and split-package suffixes remain provisional and should not be used as hard parsing rules.
