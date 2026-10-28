# Plugin runtime provenance

These GPL-3.0-or-later helpers are adapted from the user-supplied TacHen source
archive (upstream commit db0556bee04d75816bc3039d479ebbbed6b3c325).
Upstream: https://github.com/GronedWaffel/TacHen

Imported: platform/native/{elf-image.c,elf-image.h,elf-map.c,prx-image.c,
prx-image.h,process-info.c,process-info.h,game-info.c,game-info.h,game-source.c,
game-source.h,game-metadata-time.h}. Changes: private hhp_/Hhp namespace,
C linkage headers, and the existing HyperHEN ptrace interface.

The PRX container layout follows etaHEN/etaHEN-Plugins. Standalone native process
creation uses HyperHEN's existing John Tornblom libelfldr-derived code; its
original notices remain in source/libonion_elfldr/source/elfldr.c.

The staging, worker and receipt integration follows TacHen's documented native
plugin workflow, adapted to HyperHEN's Toolbox and private loader. This does not
import TacHen's UI, runtime, exploit or firmware profile tables.
