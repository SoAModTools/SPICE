# SST/SML File Records

## SML Resource Roles

Each SML record identifies one embedded MLD-like stage resource. Common roles include the main arena, floors or collision surfaces, scenery and props, animated or deforming surfaces, overlays, and optional stage variants. The record index links that resource to the SST record at the same position; exact object names remain stage-specific.

## SST Command Families

| Type | General role |
| ---: | --- |
| `0` | Base model/resource setup and transform data; one appears at the start of each block. |
| `1` | Stage lighting and render-environment setup. |
| `2` | Weighted model-coordinate deformation, used for moving liquid-like surfaces. |
| `3` | One-time texture-coordinate adjustment on selected mesh strips. |
| `4` | Per-frame object transform delta. |
| `6` | Unobserved scalar object adjustment supported by the command walker. |
| `7` | Unobserved sine-driven object adjustment supported by the command walker. |
| `8` | Node-oriented texture or presentation animation setup. |
| `9` | Model/object orientation setup. |
| `10` | Vector interpolation or oscillation setup. |
| `11` | Rare vector-motion controller with associated ramp or hold data. |

These roles describe command families, not a complete field-level editing contract. Their target model is selected within the paired stage resource context.
