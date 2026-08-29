# STD File Records

## Action-Row Families

Action rows are grouped by callback index. Broad known families include normal model-motion callbacks, menu or child-task callbacks, setup callbacks, and preserve-only specialized callbacks. The callback determines whether the ordinal, auxiliary parameter, timing, progress, or flags have a defined meaning.

## Entry Payload Families

| Combined type | Label | General role |
| ---: | --- | --- |
| `0x0003002A` | `SYSTEM CAMER` | Action-view camera timing and mode request. |
| `0x0003002E` | `ICON CONTROL` | Battle icon presentation control. |
| `0x00030036` | `SE REQUEST` | Sound or effect request. |
| `0x0003003B` | `STREAM SET` | Stream or presentation-resource control. |
| `0x00030049` | `CALL CHARA` | Character or model resource request. |
| `0x00030053` | `PUTMODEL-` | Model placement and ownership data. |
| `0x00030057` | `EXT CHARA` | Extended character data; interpretation remains provisional. |
| `0x00030058` | `SYS CAMERA2` | Extended camera request with guarded submodes. |

Several payload families begin with related action-selection keys, but their later fields are specific to the selected type and mode. Unknown combined types remain valid opaque payloads.
