# Provenance

`reverb-core` (`Fdn.hpp`, `Fdn.cpp`) and the two `dsp-kit` headers it needs (`DelayLine.hpp`, `Filters.hpp`) are copies from the Echoviard repository, revision `1e521ddfcdb20bdd1a72c1915c0d966b7f54ae50` (2026-10-31). They are not a shared module: Genetic Composer builds and changes its own copy. The FDN feeds only the internal audition monitor and never touches outgoing MIDI.
