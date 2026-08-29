# SCT File Records

SCT opcodes form broad instruction families. Only high-confidence, generally useful roles are summarized here; the complete numeric table and per-script details are outside this overview.

| Family           | Representative operations                             | General role                                                                      |
| ---------------- | ----------------------------------------------------- | --------------------------------------------------------------------------------- |
| Control flow     | If, switch, jump, subscript call, return              | Selects paths and transfers execution within or between script sections.          |
| Scheduling       | Scheduled and skip-refresh prefixes                   | Delays or changes update behavior for the following instruction.                  |
| Labels and text  | Label/string prefix and dialog-oriented operations    | Defines string groups and references text used by scripted presentation.          |
| Resource loading | MLD and script-name loads                             | Requests model or script resources named in operands or footer strings.           |
| Area transitions | Current-area warp, overworld return, ship-battle exit | Moves between field, overworld, or battle contexts using serialized destinations. |
| Generated UI     | Reputation or wanted-list dialog                      | Builds specialized presentation from labels and game state.                       |

Unknown opcodes should retain their numeric value and raw operand words until their serialized contract is established.
