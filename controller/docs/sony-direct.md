# Sony DIRECT transition selection

faderOS 0.25.0 keeps WIPE and DME number selection independent. Transition Type
opens its family's keypad preparation; keypad preparation never arms a different
transition family. With kavtor, DME DIRECT is available when the server advertises
`sonyDmes`. kavtor 0.16 advertises 44 reviewed Sony DME codes and 83 Sony WIPE codes.
Generic DME codes 0–9, 12–14 are local project shortcuts, not Sony identifiers.

Press DIRECT to edit the remembered number in the active family. Digits change a
draft and ENTER blinks until accepted. ENTER commits only a supported number;
invalid values beep and leave the previous active selection intact. Another
function cancels the draft. Press DIRECT again to return to that family's keypad
shortcuts; reopening restores its last committed number. DME selections are
retained independently across M/E delegation. AUTO and T-bar use the committed
choice; NORM/REV remains operational.

WIPE DIRECT learns the server's `sonyWipes`, including the expanded catalogue supplied by
casparMIX 0.10.1 through kavtor 0.16. Other adapters retain their own capability
lists and operations. No Sony panel firmware update is needed.
