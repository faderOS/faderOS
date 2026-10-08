# Public source and publication workflow

faderOS is published under GNU GPLv3 from its standalone repository. The
reviewed import contains `controller/` and `firmware/`, with original branding
and English guides. The private research workspace/history is not the public
repository. Sony ROMs, manuals, media, active endpoint settings, credentials
and hardware backup bundles are excluded.

Changes follow issue → focused branch → pull request → reviewed merge.
Keep CI green, preserve license/attribution, and validate hardware changes
on the physical panel. Local socket peers and host tests are not physical
firmware acceptance. Brand spelling is exactly `faderOS`; kavtor is the renamed
mixer adapter, with saved numeric backend IDs retained.

Source exports should use committed public Git trees. Controller-only archive
tooling is available under `controller/tools`; firmware update clients retain
a small diagnostic Python protocol library under `controller/python`.
