# Harmonic transition prior

The embedded prior is derived from **Bach Choral Harmony**, by Daniele P.
Radicioni and Roberto Esposito (2014), UCI Machine Learning Repository.

- Source and citation: https://doi.org/10.24432/C5B61F
- Dataset page: https://archive.ics.uci.edu/dataset/298/bach+choral+harmony
- License: **Creative Commons Attribution 4.0 International (CC BY 4.0)**,
  https://creativecommons.org/licenses/by/4.0/
- Unmodified source: `uci-bach/jsbach_chorals_harmony.data` and `.names`.
- Data SHA-256: `81554b251ca9183686b0aa207f214ea92fd48dbb73ef89fa1678a257d3a57457`.
- Archive SHA-256: `6b5a0d52262839ead7605b8b95f455d464299a245638f44990fcb443bd0f4aaf`.

The source has 5,665 annotated events in 60 chorales. Whitespace is removed
from source IDs before grouping (otherwise two IDs appear twice). Events stay
within their source sequence; adjacent events with the same root and coarse
major/minor/diminished family are collapsed. Inversion, metrical subdivision
and extension changes do not become extra root transitions. All 101 original
chord labels are parsed; no rows are silently skipped.

`scripts/train-harmony-model.py` produces `HarmonyModelData.h` from **2,890
root/family changes**. The representation is transposition invariant: the next
root interval and family are conditioned on the current family and the previous
root interval. It retains neither complete chorales nor their melodies. Counts
back off to a first-order family model with additive smoothing. This is a small
statistical prior, not a neural model or a style-complete music dataset.

## Rebuild and validation

Run `python scripts/train-harmony-model.py` to regenerate, or append `--check`
to verify byte-for-byte reproducibility. Normal builds use the checked-in header
and require neither Python nor a network connection. `--download` explicitly
downloads the checksum-pinned UCI archive.

A stable SHA-256 split by source ID holds out 9 complete chorales (421
transitions with sufficient context). On that held-out split, perplexity is
**36** for uniform choice, **8.427** for the smoothed first-order model and
**7.823** for the interval-context model. The generated file records the exact
measurements. After validation, the deployed counts are fitted to all 60
chorales. These figures measure prediction within this classical corpus;
they do not establish subjective musical quality or jazz/pop performance.

The generator mixes the prior with original phrase planning, harmonic palettes,
cadence constraints, common-tone preferences and repetition costs. Its weight
is deliberately lower in Pop, Jazz, Modal and Cinematic than in Balanced.
Those musical profiles are authored rules, **not styles learned from Bach**.

The derived model data are distributed under CC BY 4.0, with attribution above.
The generator and training-script code remain under the project's MIT license.

Other sources investigated include DCML's annotated Mozart sonatas
(https://github.com/DCMLab/mozart_piano_sonatas, CC BY-NC-SA 4.0) and ChoCo
(https://github.com/smashub/choco, collection-specific terms). They are not
included in or used to train this model.
