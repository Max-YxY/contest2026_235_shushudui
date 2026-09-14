# ESP32-P4 OpenVela Project Progress Ledger

##### [2026-09-10] v2.69 energy-level payload fidelity diagnosis (no flash)

- User-reported energy-level errors were reproduced from the exact `v2.69`
  `p4-v9-demo10.ovip` bytes, rather than from the source JPEG files.  Added
  `tools/audit_ovip_energy_payload.py`, which parses the actual OVIP4CAR
  payload, applies the firmware's `336x200 RGB565 -> 1024x600` nearest
  expansion, the production cyan locator, its centred 80% inference ROI and
  the deployed INT8 TFLite input conversion.
- The strict replay reports only `2/10` correct energy levels.  Thus the
  previously stated source-image `10/10` result was not a valid board-path
  acceptance result and must not be used to claim energy accuracy.
- Inspection of the raw payload confirms why: a full product image is reduced
  to `336x200` before processing, leaving the 1--5 grade mark only a very
  small fraction of the stored frame.  Enlarging only the full class-8 label
  crop without changing the model also failed (`3/10`), so resolution alone
  cannot repair a model trained against the wrong semantic ROI.
- A first dedicated grade-mark experiment (`class 0`, 96x96, RGB565/bilinear)
  reached `84.21%` held-out accuracy, but it is not a deployable candidate:
  class IDs 0--4 vary by energy grade, so this experiment is evidence for the
  correct small-ROI direction only.  The next offline work is to train and
  validate a variable grade-mark ROI path, then prove exact OVIP payload
  replay before creating any versioned application or gallery write.
- No application, gallery, or protected model Flash was written or erased in
  this diagnosis.

##### [2026-09-10] grade-mark training contract corrected (no flash)

- Corrected an error in the first grade-mark experiment: the five energy
  levels are represented by different annotation classes (`0..4`), not by a
  fixed class `0`.  Added `--roi-class -2` to select the actual grade-mark
  annotation from every image.
- The corrected 96x96 RGB565/bilinear grade-mark model reached `98.95%`
  (`282/285`) on the held-out set.  This confirms that the discriminative
  information is present once the grade mark is given enough effective pixels;
  the observed board failure is principally the full-product/large-label
  crop contract, not a lack of model capacity.
- This result is still offline evidence only.  Next gate is to define a
  deterministic board-side grade-mark crop relative to the verified label
  ROI, then reproduce that exact crop during training and on an OVIP payload.
  No candidate is eligible for a board write until that payload replay passes.

##### [2026-09-09] gallery energy INT8 conversion gate: conversion passed; board-preprocess parity pending (no flash)

##### [2026-09-09] ROI energy classifier v7 passed host and INT8 gates (no flash)
- Added a class-8 label-ROI crop to the compact energy classifier. The 96x96 bilinear ROI model (`35,009` parameters) reached `88.0702%` on all 285 held-out images, improving materially over full-gallery inputs.
- Converted the ROI model to full INT8 TFLite (`42,928` bytes, SHA-256 `8bc41e21e91d8fe6fdfeab15201397cc46a9e19e1db87a8c5cf2e7f46c9b07c1`). INT8 accuracy remains `88.0702%` and argmax agreement is `285/285`.
- The candidate operator set is `PAD`, `CONV_2D`, `TRANSPOSE`, `RESHAPE`, `FULLY_CONNECTED`; the existing diagnostic resolver now explicitly registers INT8 `FULLY_CONNECTED` in both initialization paths. This is a source change in the staging diagnostic candidate only, not a flashed image.
- Remaining gates are cross-build, arena/memory/timing measurement, and one isolated board diagnostic. No application, gallery (`0xEB3000`), or model (`0x410000`) Flash was written.

##### [2026-09-09] v2.60 ROI diagnostic asset and VM build check (no flash)
- Archived the ROI INT8 model as the `v2.60` diagnostic asset at `apps/examples/camera_diag/models/energy_roi_v7_int8.tflite` in the VM project. Model SHA-256 is `8bc41e21e91d8fe6fdfeab15201397cc46a9e19e1db87a8c5cf2e7f46c9b07c1`; the previous `best_full_integer_quant.tflite` was not overwritten.
- Added INT8 `FULLY_CONNECTED` resolver registration to the staging TFLite diagnostic source in both tensor-allocation paths. The TFLite Micro source tree provides `Register_FULLY_CONNECTED_INT8()`.
- VM cross-build completed with `riscv32-esp-elf-gcc` and `esptool.py`; generated `nuttx/nuttx.bin` SHA-256 `b8970cbc18b90423e8be2ec53b6e946a09e321c6977207c252c76c197643386b`, size `375,796` bytes.
- At the time of this initial asset check, the VM `camera_diag/Makefile` still excluded `camera_diag_tflm_init.cc` from `CXXSRCS`; this was later corrected in the v2.60 candidate entry below. No board, application Flash, gallery Flash (`0xEB3000`), or model Flash (`0x410000`) was written.

##### [2026-09-09] v2.60 TFLite ROI source integration cross-build passed (board test required)
- Renamed TFLite sources to the OpenVela-supported `.cxx` extension, added required TFLite Micro include paths/flags, and included model storage plus TFLite initialization/preprocess sources in the VM candidate `camera_diag/Makefile`.
- Cross-build and ESP32-P4 image packaging completed successfully with `riscv32-esp-elf-gcc` and `esptool.py`. Image SHA-256 remains `b8970cbc18b90423e8be2ec53b6e946a09e321c6977207c252c76c197643386b`; size `375,796` bytes.
- The model asset `energy_roi_v7_int8.tflite` remains separate from the existing model file. No model partition (`0x410000`), gallery partition (`0xEB3000`), or board application area was written.
- Next required action is one controlled board diagnostic to measure model load, arena allocation, internal/PSRAM remaining memory, and single-frame inference latency. Stop before flashing unless the board is connected and a version-labelled `v2.60` application-only candidate is explicitly selected.
- Converted the 96x96 compact gallery energy classifier to a full-INT8 TFLite model (`42,928` bytes, SHA-256 `544301d8a01e6eae5694cd373f6502157a3dc198c6ce7a5848d59d14b99e4347`). It contains only `PAD`, `CONV_2D`, `TRANSPOSE`, `RESHAPE`, and `FULLY_CONNECTED` nodes; it has no ESP-DL runtime dependency.
- The host validation used gallery RGB565 plus a **bilinear** 96x96 resize. It reports ONNX and INT8 accuracy of `81.4035%` and argmax agreement of `284/285` (`99.6491%`), proving the conversion does not materially change the host model output.
- Subsequent source audit found that the existing board helper uses **nearest-neighbour** resize. Therefore the model is not board-path eligible yet: it must be retrained and revalidated with that exact resampler before any TFLite Micro integration or board timing candidate is prepared.
- No board, application Flash, gallery Flash (`0xEB3000`), or protected model partition (`0x410000`) was written.

##### [2026-09-09] nearest-neighbour board-preprocess retraining did not meet accuracy gate (no flash)
- Retrained the same 35,009-parameter 96x96 classifier with the exact existing board nearest-neighbour RGB565 resize rule. Held-out validation accuracy was `76.8421%` (`219/285`), below the `80%` deployment gate. This model is rejected from firmware integration.
- The earlier `81.4035%` bilinear-path model remains conversion evidence only, not a board-path candidate. Next offline experiment is a modest 128x128 nearest-neighbour input; it remains small enough for an isolated TFLite Micro diagnostic but must independently meet the same gate.
- No board or Flash region was modified.

##### [2026-09-09] final bounded compact-model experiment and controlled-set check (no flash)
- The wider 96x96 bilinear classifier (58,197 parameters) reached `76.4912%` held-out accuracy, so capacity increase did not clear the 80% gate and the candidate is rejected.
- Checked the existing ten-image controlled gallery with the prior host-only bilinear small model: 9/10 levels are correct; the current `normal-level-5` is misclassified as level 4. It must not be presented as a fully correct energy-recognition gallery.
- The stable OpenVela UI/capture/gallery/defect workflow remains untouched. No firmware candidate was built or flashed; application Flash, gallery Flash (`0xEB3000`), and protected model Flash (`0x410000`) remain unchanged.
- Conclusion: a board-path-valid compact energy classifier is not yet available from the current dataset/preprocessing route. Continue only with a stronger labelled energy ROI dataset or an explicitly constrained, independently validated controlled-gallery feature; do not label static metadata as arbitrary-image inference.

##### [2026-09-09] defined 96x96 bilinear-preprocess retraining rejected (no flash)
- A C++-reproducible centre-aligned bilinear preprocessing formula was added to the host trainer. The original-width classifier achieved `75.7895%` held-out accuracy, below the deployment gate, so it is rejected.
- One final bounded host-only experiment will increase the compact classifier width while retaining the identical 96x96 bilinear input and standard INT8-compatible operator set. It will not be considered for firmware unless it clears the 80% gate.
- No board or Flash region was modified.

##### [2026-09-09] 128x128 nearest-neighbour follow-up rejected (no flash)
- The 128x128 nearest-neighbour retraining produced `73.3333%` held-out accuracy, below both the 96x96 nearest result and the deployment gate. It is rejected; increasing input dimensions under this nearest path is not a valid next step.
- The next experiment is a defined, C++-reproducible bilinear downsampler before the 96x96 INT8 model. Its cost is only 9,216 destination pixels and will be measured on board only after host accuracy, output parity, resolver coverage, and cross-build gates pass.
- No board or Flash region was modified.
- No board, application Flash, gallery Flash (`0xEB3000`), or protected model partition (`0x410000`) was written.

##### [2026-09-09] energy-level board-path gate completed (no flash)
- Ran the 36 energy-source-verified defect candidates through the exact production path: `336x200 RGB565 -> 1024x600 nearest expansion -> lightweight locator/anomaly/multiclass head`. The accepted pool is 6 `DAM`, 9 `STA`, and 8 `WRI`; rejected candidates are excluded from any controlled-gallery proposal.
- A proposed ten-image controlled set covers energy levels 1--5 and includes board-path-confirmed stain, damage, and wrinkle representatives. Its source model energy confidence is recorded per image in `artifacts/p4-energy-gallery-audit/controlled-energy-defect-gallery.json`.
- Re-ran two independent compact energy backends using gallery-equivalent RGB565 inputs. The level-mark template baseline is 28.0% on held-out data, color statistics is 54.3%, and level-mark luma/Sobel SVM is 31.3%. None meets a deployment threshold.
- All 20 source-model-verified normal candidates become label-not-found after the current `336x200` gallery resize. They are not defect false positives, but cannot validate on-board label localisation or an energy classifier at this gallery resolution.
- No firmware, gallery Flash (`0xEB3000`), or protected model partition (`0x410000`) was written. Do not claim current gallery metadata as arbitrary-image on-board energy recognition. A real energy feature now requires either a higher-resolution image path plus runtime/latency validation, or a clearly named pre-validated controlled-gallery field.

##### [2026-09-09] higher-resolution energy gallery comparison (no flash)
- Extended `tools/audit_p4_energy_gallery.py` so the audit can reproduce configurable RGB565 gallery geometries instead of assuming `336x200`.
- Compared the controlled ten-image set through both `336x200 -> 1024x600` and `512x300 -> 1024x600` nearest-neighbour panel paths before the existing source ONNX model. Both paths correctly classified all ten selected images above the `0.65` confidence gate; the `512x300` confidence values were not materially higher.
- Therefore a four-image `512x300` gallery would reduce gallery capacity without producing evidence that it fixes energy recognition. It also cannot by itself solve the unvalidated, slow model-runtime integration on the stable OpenVela application.
- The stable board image, gallery Flash, and model partition remain untouched. No `v2.59` firmware candidate was built or flashed because the required real on-board energy backend has not passed its accuracy/latency gate.

##### [2026-09-09] v2.57 gallery defect false-negative investigation
- User reported that carousel page 03 (`DAM`) and page 10 (`STA`) both render `外观状态 正常`, although the red position result is shown.
- Added `tools/p4_board_path_harness.cxx` and `tools/run_p4_board_path_regression.py`. The host harness compiles the production RGB565 locator, feature extractor, anomaly model and multiclass head, and reproduces the gallery's `336x200 -> 1024x600` expansion exactly.
- The current deployed carousel package (`215f5c7c0d46c5e9751bc4fba3ae3447e7b0b3d0f8910abe86fe42634ebd5755`) is confirmed byte-for-byte locally. Its actual page 03 scores `DAM`, abnormal, score `959`; page 10 scores `STA`, abnormal, score `20529`. This differs from the reported LCD result and must not be dismissed as a bad source image.
- Board connection was read-only verified as ESP32-P4 v3.2 / MAC `e8:f6:0a:e3:a9:5c`; no application, gallery, or model area was written. A `0x2000..0x51FFF` application readback was captured for version reconciliation.
- Next: create `v2.58` with a high-confidence multiclass defect gate and explicit review-frame C2M cache synchronization; compile and run the board-path regression before any application-only flash. Model partition `0x410000` and gallery package area remain protected.

##### [2026-09-09] v2.58-defect-gate-cache-sync flashed
- The candidate passed the exact board-path regression: carousel page 03 is `DAM`, abnormal, high confidence, score `959`; page 10 is `STA`, abnormal, high confidence, score `20529`.
- Full ESP32-P4 cross-build passed. Application SHA-256: `b8970cbc18b90423e8be2ec53b6e946a09e321c6977207c252c76c197643386b`; size `375,796` bytes.
- A read-only recovery image of the previous application range `0x2000..0x51FFF` was archived before writing; SHA-256 `e6ca94c3d388fdf3d9501731eeec944ddb61ee356c9be49c1c2e05bdb82fb789`.
- `v2.58` was written and independently verified at application-only range `0x2000..0x5DBF3`. Esptool reported `Hash of data verified`, then `Verification successful (digest matched)`. The board was reset.
- Gallery package `0xEB3000` and model partition `0x410000` were not accessed or modified. Pending physical acceptance: page 03 must display `破损`, page 10 must display `污渍`; UI and gallery navigation must remain normal.

##### [2026-09-09] v2.58 rollback and energy-level workstream
- User reported v2.58's visible result was worse than the preceding version. The captured pre-v2.58 application image was restored at `0x2000..0x51FFF`; both write-side and independent verification passed. No gallery or model region was modified.
- Energy-level recognition is a separate requirement. The source dataset defines five energy classes (`level_1` through `level_5`), and the legacy YOLO decoder already specifies the class mapping. It is not yet safe to wire that runtime into the stable gallery: the current lightweight gallery route does not invoke the YOLO model, and its protected model partition must remain unchanged. Next work is an offline compatibility and latency check against the existing protected model before a UI candidate is prepared.

##### [2026-09-09] energy-level offline compatibility audit
- Added `tools/audit_p4_energy_gallery.py`, which applies gallery RGB565 quantization and panel expansion before evaluating the P4-source YOLO ONNX split head. It records label-derived truth separately from model output.
- Across the 20 controlled candidates, 17 levels were correct but only 10 met the conservative `>=0.65` confidence gate. The current 10-page mixed-defect gallery contains six accepted level predictions; it must not display the other four as confirmed.
- Added `tools/select_energy_verified_gallery.py`. It found two high-confidence, correct normal samples for each of levels 1 through 5 after scanning 11 validation images. These form a dedicated energy demonstration set, not a replacement for the defect set.
- No board write occurred. Next: select high-confidence energy samples that also cover the three defect classes, then decide whether to add a lightweight on-board classifier or to expose an explicitly offline-verified gallery field. The latter must not be represented as board-time recognition.

##### [2026-09-09] energy-and-defect controlled selection
- Extended `tools/select_energy_verified_gallery.py` to require high-confidence representatives for stain, damage, and wrinkle as well as levels 1 through 5.
- After scanning 82 validation images, it found two level-verified normal samples for each level and three additional verified defect samples: `A02_L1_T02_DAM_004`, `A02_L1_T03_STA_024`, and `A02_L1_T05_WRI_024`; all three are level 1 and have source-model confidence `0.827`, `0.815`, and `0.795` respectively.
- This is a source-model controlled-selection result, not a claim of board-time inference. Before the UI can call it recognition, the selected images must pass the actual RGB565 lightweight defect path and the chosen board-side energy backend must be benchmarked without changing the protected model partition.

##### [2026-09-07] v2.27 PC-uploaded image import candidate built (not flashed)
- Strategy changed from camera-dependent recognition to PC-provided source images. The board now has a single-image upload format: the PC letterboxes JPG/PNG to `1024x600` and converts it to RGB565 little-endian, with a 64-byte versioned header and payload SHA-256.
- A read-only board scan established that `0xEB3000..0xFFFFFF` is an all-`FF`, continuous 1,363,968-byte range. It is outside the application (`0x2000..0x4C8FF`) and protected model (`0x410000..0x80FFFF`) regions. `0x810000` was not assumed safe because it contained existing data.
- `v2.27` makes OpenVela only *read* and validate that upload slot when Gallery is selected; it does not erase or program Flash. An uploaded valid image takes precedence in Gallery, otherwise the accepted camera-gallery frame is retained.
- Complete cross-compile/link passed. Candidate SHA-256 `61c47f2480faac0f42d186f5752ff82bf0650cb67e2e6fd3f387e9ef97dc80bd`, size `307572`, intended application-only range `0x2000..0x4D173`. No board write and no upload-slot write has occurred. The next board gate is strictly “uploaded test image displays correctly”; rule/model detection is not yet included in that gate.

##### [2026-09-07] v2.27 application and first test image written (physical acceptance pending)
- `v2.27-upload-image-import` was written and independently verified at `0x2000..0x4D173`; SHA-256 remains `61c47f2480faac0f42d186f5752ff82bf0650cb67e2e6fd3f387e9ef97dc80bd`.
- The PC-converted package from the supplied JPG was written and independently verified only at `0xEB3000..0xFDFFFF` (1,228,864 bytes, package SHA-256 `b79e672db571f124ff14e5c4b1804747330be58abe61112f44d580f30826fa99`). The remainder of the verified empty upload slot and the model region were not accessed.
- Awaiting physical acceptance: normal UI, persistent touch, then Gallery displays the PC-provided image. This validation intentionally does not include camera capture or rule/model recognition.

##### [2026-09-07] v2.28 PC-image-only UI written (physical acceptance pending)
- User requested removal of camera dependency. The product touch path no longer calls the preview/capture transaction, so it cannot initialize camera, CSI, ISP or the camera-side DMA route. Gallery is the sole product image action and loads only the PC-uploaded RGB565 image.
- Candidate `v2.28-pc-image-only`, SHA-256 `ad1ea2d058ae23a0309a85f91f5500e2ffbc5ed0c2641e64ac8a1c44ce8efcc6`, was written and independently verified only at `0x2000..0x4D01F`. The existing image package at `0xEB3000..0xFDFFFF` and the model region remain untouched.
- Awaiting physical acceptance: UI boot, persistent touch, Gallery displays the PC-uploaded image, and tapping the image returns to the UI.

##### [2026-09-07] v2.29 full-screen PC-image entry written (physical acceptance pending)
- The uploaded image was independently re-verified in Flash at `0xEB3000..0xFDFFFF`; it is present and has the expected package digest. The prior Gallery failure is therefore not an absent upload.
- `v2.29-pc-image-any-tap` removes the narrow Gallery hit-test from the product path: any main-screen touch/release opens the uploaded image, and any image-screen touch returns. Camera/CSI/ISP remain unreachable from product touch.
- SHA-256 `078f6f2f039fd3703a153fef30c54bb9456cb475f922d3fbd3e65903007f4c7f`; only `0x2000..0x4D007` was written and independently verified. Model and upload-image regions were not accessed by this flash.

##### [2026-09-07] v2.30 touch-down image open written (physical acceptance pending)
- User reported that v2.29 still did not open the image. Root cause in product input flow: opening waited for a GT911 zero-contact/release report, which is not reliable on this page. `v2.30` opens on the initial touch-down event instead.
- An opening-touch release guard prevents the same tap from immediately returning. After the image is open and the opening finger is released, the next touch/release returns to the main screen.
- Candidate SHA-256 `af34ada206e3a7dd7ff46023b59ae3a6ecf6b82885c5c081a2fe1b6a435e108e` was written and independently verified only at `0x2000..0x4D027`. The upload image and model region are unchanged.

##### [2026-09-07] v2.31 16 MB runtime Flash image written (physical acceptance pending)
- Root cause refinement after repeated no-display reports: v2.27–v2.30 embedded a 4 MB Flash configuration, while the verified uploaded image sits at `0xEB3000` on the physical 16 MB Flash. The downloader can access the slot, but the OpenVela runtime Flash driver may reject it.
- A read-only 4 MB-range audit found no safe one-image blank area before `0x410000`; it was not overwritten. The isolated storage correction is therefore a 16 MB image-header/runtime configuration, matching the downloader's hardware report.
- `v2.31-flash16-upload-read` was fully rebuilt; `esptool image-info` confirms 16 MB/DIO/80 MHz. SHA-256 `2a72f736a9ea4034d8276bc2e863bac07444ddaa5354f309546f0f0ac38cbf3a`; application-only write/independent verify passed at `0x2000..0x4D027`. The existing upload package and model content were not written.

##### [2026-09-06] v2.7 UI-owned lightweight rules candidate built (not flashed)
- Preserved the physically accepted v2.6 single-frame capture transaction. The persistent UI task now evaluates the captured RGB565 frame with the fixed-workstation cyan-label and appearance-rule component before the existing five-second review display.
- The rule path uses fixed static scratch memory and no ESP-DL, TFLite, model storage, dynamic allocation, external NSH capture command, or real-time preview. It emits one `[UI_RULES]` UART result; image annotation and Chinese result rendering remain out of scope for this first integration candidate.
- Full ESP32-P4 cross-compile and link passed. UI-owned capture ownership and camera cleanup audits passed. Image SHA-256 is `afc65b37d0dcf9044677cf3948cd9d97a9fd2367e6ba93d780ded39bf4358ed5`, size `303232`, intended only for `0x2000..0x4c07f`.
- The protected model region starts at `0x410000` and is outside the candidate range. The board remains on v2.6; do not flash v2.7 without separate authorization and physical acceptance. Recovery remains `recovery-range-0x002000-0x134000.bin`, SHA-256 `fdf5de8d371f45c0b335c29794413d6f53acea3fa728a8197a278a6fd0161b0c`.

##### [2026-09-06] v2.7 UI-owned lightweight rules flashed (physical acceptance pending)
- User explicitly authorized `v2.7-ui-owned-lightweight-rules`. Candidate SHA-256 `afc65b37d0dcf9044677cf3948cd9d97a9fd2367e6ba93d780ded39bf4358ed5` was rechecked before writing. VM download port `/dev/ttyACM0` identified ESP32-P4 v3.2, MAC `e8:f6:0a:e3:a9:5c`.
- Esptool wrote only `0x2000..0x4c07f` (303,232 bytes); write-side hash verification and an independent `verify-flash` with `--flash-mode dio --flash-freq 80m --flash-size 16MB` both passed. Model region `0x410000` was not accessed. Board was hard-reset after verification.
- Awaiting the user's physical observation of normal UI boot, one central capture action, single-frame review/return, and continued touch. No external camera command is to be run during this acceptance window. Recovery package remains `recovery-range-0x002000-0x134000.bin`, SHA-256 `fdf5de8d371f45c0b335c29794413d6f53acea3fa728a8197a278a6fd0161b0c`.

##### [2026-09-06] v2.7 physical acceptance passed
- User confirmed the normal UI, single-frame display/return, and touch all remain functional after flashing v2.7. No black screen, blue screen, flicker, restart, or touch regression was reported.
- The candidate is accepted as the first stable UI-owned capture plus lightweight-rule execution baseline. Next work may proceed offline toward result-state/annotation integration; do not alter the accepted capture ownership path.

##### [2026-09-06] v2.8 UI-owned rule overlay candidate built (not flashed)
- Based on accepted v2.7. The captured RGB565 review frame now receives a simple overlay after rule evaluation: green rectangle for normal detected label, red rectangle for suspected appearance anomaly, or red footer when no label is found.
- No camera transaction, touch path, DSI/CSI ownership, model, model partition, dynamic allocation, or external NSH command changed. Full cross-compile/link, UI ownership audit, and camera cleanup audit passed.
- Candidate image SHA-256 `be910cf724a5861d22b36786d5951fe313b61d115ef2358906aa729f5483e17e`, size `303456`, intended range `0x2000..0x4c15f`; model region `0x410000` remains protected. Board remains on accepted v2.7 pending separate physical authorization.

##### [2026-09-06] v2.8 flashed (physical overlay acceptance pending)
- User granted standing authorization for this candidate class under the existing version/hash/range/model-protection rules. Candidate SHA-256 was rechecked before writing; `/dev/ttyACM0` identified ESP32-P4 v3.2 with MAC `e8:f6:0a:e3:a9:5c`.
- Esptool wrote only `0x2000..0x4c15f` (303,456 bytes). Write-side hash verification and independent `verify-flash` with matching `dio/80m/16MB` parameters passed. Model region `0x410000` was not accessed. Board was hard-reset.
- Awaiting physical observation of the green/red rule overlay, normal single-frame review/return, and continued touch. Rollback remains the verified recovery package `recovery-range-0x002000-0x134000.bin` with SHA-256 `fdf5de8d371f45c0b335c29794413d6f53acea3fa728a8197a278a6fd0161b0c`.

##### [2026-09-06] v2.8 physical overlay acceptance passed
- User confirmed the v2.8 UI and interaction remain normal. The colored rule overlay is accepted as the current visual result baseline.
- Next offline task is to add position-deviation/geometry status and Chinese result wording without changing the accepted UI-owned capture transaction. No new board write is started in this step.

##### [2026-09-06] v2.9 geometry report flashed (physical acceptance pending)
- Added `[UI_GEOMETRY]` UART output after successful lightweight label detection: ROI center in permille, offset from frame center, and ROI area ratio. No threshold or product pass/fail claim is made until fixed-fixture calibration is completed.
- Capture, overlay, DSI/CSI ownership, touch, model, and partition paths are unchanged. Full cross-compile/link, ownership audit, and cleanup audit passed.
- Image SHA-256 `d64eb4e3d404adc1eb3f0e4d17702ea4bb08b2f297756549981c8745a97c2125`, size `303548`, written and independently verified only at `0x2000..0x4c1bb`. Model region `0x410000` was not accessed. Awaiting physical confirmation of stable UI, capture/return, touch, and no regression.

##### [2026-09-06] v2.3 camera-stage diagnostic board evidence (no flash)
- The already deployed `v2.3-camera-stage-diagnostics` image was exercised once through UART0 without a flash write. VM USB paths were present as `/dev/ttyACM0` (download) and `/dev/ttyUSB0` (UART0).
- Exact UART result: `csi_isp: fixed WBG enabled (R=512 G=256 B=512)` followed by `[CAMERA_STAGE] isp isr rc=-5`, then an immediate `nsh>` prompt. The capture, model, rule, and decode paths were not entered.
- `-5` is the application-level `-EIO` mapping for a negative return from `esp_setup_irq(DW_GDMA_INTR_SOURCE, ...)` inside `esp32p4_csi_install_block_isr()`. This narrows the next offline investigation to DW-GDMA interrupt source/CPU-interrupt mapping. It is not evidence of a model-performance bottleneck.
- The UART record is retained in the running VM at `/tmp/v23-camera-stage-20260906.log`. No model region or flash address was accessed by the test.

##### [2026-09-06] v2.3 diagnostic rejected and stable range restored
- A second `camera_diag --isp-dsi-preview` attempt reproduced `[CAMERA_STAGE] isp isr rc=-5`; the user observed a blue screen. The v2.3 camera-stage diagnostic route is rejected for further board testing.
- The verified recovery image `recovery-range-0x002000-0x134000.bin` was restored immediately through `/dev/ttyACM0`. The recovery script validated size `1,253,376` bytes and SHA-256 `fdf5de8d371f45c0b335c29794413d6f53acea3fa728a8197a278a6fd0161b0c` before writing. Esptool independently verified the written data hash.
- Restore range was exactly `0x002000..0x133fff`; it did not write `0x134000+`. No separate new model write occurred. The board was hard-reset after verification. Do not run `--isp-dsi-preview` again on the restored image until the DSI/CSI DW-GDMA ownership handoff has a separately audited fix.

##### [2026-09-06] v2.4 DW-GDMA handoff candidate built (not flashed)
- Root cause refinement: the persistent DSI UI registers `DW_GDMA_INTR_SOURCE` for its display DMA. The v2.3 single-frame preview did not release it before CSI called `esp_setup_irq()` for the same source, yielding `-EIO` and, on repeat, a blue screen.
- Candidate `v2.4-dw-gdma-handoff` changes only `isp_dsi_preview()`: it stops an active status UI before CSI setup, and restores it via a single success/failure exit. The patch, exact source snapshots, full build log, and image are in `artifacts/candidate-v2.4-dw-gdma-handoff-20260906`.
- Offline build passed: 300,324 bytes, SHA-256 `0098faa39572a7cd85f676867466cd33ada9b37ff6c1c1c24e396fd62bfded7a`, intended only for `0x2000..0x4b523`. Model region `0x410000` is outside the candidate write range.
- Audit passed: the DSI source is byte-identical to v2.3; touch-feedback (`0x1fe`), touch-release (`0x62`), and `camera_diag_main` (`0xce4`) symbol sizes are unchanged. No TFLM, ESP-DL, model-storage, or lightweight-rule strings are present. This is still an untested candidate and must not be treated as a feature release.

##### [2026-09-06] v2.4 board test rejected and rollback completed
- `v2.4-dw-gdma-handoff` was written only to `0x2000..0x4b523`; esptool verified its content hash. Boot UI and touch were accepted before the one permitted command test.
- `camera_diag --isp-dsi-preview` still returned `csi_isp: fixed WBG enabled` then `[CAMERA_STAGE] isp isr rc=-5`, and the expected `ui paused for csi` marker was absent. The user then observed a blue screen. This proves that the visible stable UI is not represented by `esp32p4_dsi_status_ui_is_active()` in this restored runtime, so v2.4 did not release the actual DW-GDMA owner.
- v2.4 is rejected. The stable recovery image was immediately restored to `0x002000..0x133fff`; its SHA-256 was verified before writing and esptool verified written content after writing. No model-region write was performed. Do not reflash or rerun v2.4.

##### [2026-09-05] v2.2 blue-screen postmortem gate (no flash)
- Board was restored with `flash-backup-pre-espdl-migration-20260901/recovery-range-0x002000-0x134000.bin`; the user confirmed the stable UI and touch are normal. The model region `0x410000` remains untouched.
- Added `tools/audit_camera_preview_error_paths.py`. It extracts `isp_dsi_preview()` and rejects sources with silent I2C/sensor startup exits, missing stage markers, or missing shutdown labels. The tool was run against `tmp/stable-recovery-source/camera_diag_main.c` and failed deterministically: all five required stage markers were absent, and both I2C and sensor initialization could return silently.
- The local `tmp/stable-recovery-source` copy (SHA-256 for `camera_diag_main.c`: `3ceff2652afaf2a36f64e8b7987da0127028b9f300e9f239b2c2abfe4f8d5afd`) still contains `camera_diag_report_lightweight_rules()`. It is therefore contaminated by the rejected v2.2 integration and must not be used as a future baseline source tree, even though the separately stored stable recovery *binary* remains valid.
- Next gate: create an isolated source tree from a verified pre-rule source snapshot, then make a diagnostics-only candidate that changes only explicit camera-stage reporting and cleanup. No rule call, C++ rule component, scratch BSS, UI layout, model partition, or inference path may be included. This candidate is not yet built and must not be flashed without a separate user authorization.

##### [2026-09-05] recovery-baseline provenance gate (no flash)
- Established the exact recovery-image identity: `/home/max/openvela-p4-stable-recovery-isolated-20260829/artifacts/exact-stable-20260829/nuttx.bin`, 300,172 bytes, SHA-256 `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`.
- Added `tools/verify_openvela_baseline_provenance.py`. A source tree is eligible as a recovery build baseline only if its image hash matches the exact recovery image and its camera application contains no lightweight-rule tokens. Python syntax validation passed. The rejected v2.2 image was used as a negative test and correctly failed with SHA-256 `93bf070593d633268c9fbd47535660cdc2a5c24b8df63cf528e75ff76ce857b7`.
- A fresh isolated copy of the historical `openvela-p4-ui-autostart-argvfix-20260830` source was created at `/home/max/openvela-p4-clean-baseline-audit-20260905`; it has no lightweight-rule reference, but its historical application hash differs from the exact recovery image. It is retained for source comparison only and is not yet an approved build baseline.

##### [2026-09-05] historical-source rebuild attempt closed (no flash)
- The isolated historical tree was rebuilt only for provenance verification. Initial failure came from eleven copied `Make.dep` files with an obsolete absolute include path. Those generated dependency files were removed only inside the isolated audit tree, allowing compilation to advance through the ESP32-P4 HAL.
- The next deterministic failure exposed a deeper provenance break: `nuttx/arch/risc-v/src/board` is a symbolic link to `/home/max/openvela-p4-integration/.../common`, whose dependency file points to the unrelated former ESP-DL probe tree. This historical snapshot is a mixed workspace, not a self-contained build input.
- No further repair is justified for this audit copy. It is explicitly disqualified as a recovery baseline. The only approved recovery reference remains the immutable exact binary SHA-256 `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`.

##### [2026-09-05] exact-baseline reconstruction resumed (no flash)
- Recovered the archive-era application inputs in a new complete isolated tree: `camera_diag_main.c.orig` SHA-256 `18f7543b...9e10a0`, `Makefile.orig` SHA-256 `8b26d5e...60fdea`, and the current frozen DSI source. The lightweight-rule source files were excluded.
- A first clean build produced `302,780 B`, SHA-256 `89f9d848...e7dfd4`, and was rejected. Root cause: the copied historical `.config` actually enables `LABEL_INSPECTION` and `TFLITEMICRO`; it is not the 300,172-byte frozen configuration despite an older handoff statement.
- The exact known-good configuration is the stable-tree current `.config`, SHA-256 `8588ebcd...417249`, with both modules disabled. A subsequent link failed because an old generated `builtin_list.c` still referenced `label_inspection_demo_main`; this is generated-output contamination, not a source defect.
- `make olddefconfig` cannot be used to regenerate this old OpenVela branch because its Kconfig parser fails on pre-existing Tricore/LVGL syntax. The current offline rebuild instead restores the known-good generated `config.h` and `Make.defs`, removes the stale Apps builtin list, and rebuilds from the isolated tree. No candidate has been created or flashed.

##### [2026-09-05] exact recovery source baseline re-established (no flash)
- The isolated tree `/home/max/openvela-p4-exact-rebuild-20260905` was rebuilt successfully from the archive-era `camera_diag_main.c.orig`, `Makefile.orig`, frozen DSI source, and the exact known-good disabled-model configuration. The stale Apps builtin list was replaced with the matching stable generated list.
- Result: `nuttx.bin`, 300,172 bytes, SHA-256 `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`, exactly matching the immutable recovery image. The build log records successful final ESP32-P4 image generation.
- This is now the approved clean source baseline for offline diagnostic work. The prior v2.2 source tree remains rejected and must not be used. No Flash operation occurred.

##### [2026-09-05] v2.3 camera-stage diagnostic candidate ready (not flashed)
- Candidate `v2.3-camera-stage-diagnostics` built successfully from the exact recovery source baseline: 300,328 bytes, SHA-256 `8c357f6d39eb568ca508d4dae75eed602e266790b47dc735081a04dd75d688c9`, intended for `0x2000..0x4B527`. It is stored at `artifacts/candidate-v2.3-camera-stage-diagnostics-20260905` with a complete manifest and verified recovery package.
- Source-level audit passed: all I2C, sensor, CSI, ISP, and capture exits identify a `[CAMERA_STAGE]` result; the former I2C/sensor silent returns are absent. The successful CSI/ISP/DSI call order is unchanged.
- Binary audit: touch-feedback and touch-release function sizes remain `0x1fe` and `0x62`; DSI status drawing remains `0x55e`; `camera_diag_main` remains `0xce4`. Their addresses shift uniformly by `0x9a` because the candidate adds 156 bytes. No lightweight-rule, model, UI, or touch source is present.
- VM preflight passed with ESP32-P4 v3.2 at `/dev/ttyACM0`; candidate and recovery SHA-256 values, write end, and protected model boundary `0x410000` were checked. No Flash write occurred. Pending only explicit user authorization and physical display/touch observation.

##### [2026-09-05] 电脑端 ONNX 图优化与算子基准（未刷写）
- 对原始 `best.onnx`、分离输出 ONNX 及现有 ESP-DL 对应的简化 ONNX 做了只读结构检查。
- 原始图为 234 个节点：64 个 Conv、58 个 Sigmoid、58 个 Mul 等；`onnxsim` 未发现可消除的结构冗余（分离输出简化图仍为 233 个节点）。
- 在同一随机输入、ONNX Runtime CPU、启用全部图优化下，320×320 单次前向平均约：原始 6.82 ms、分离输出 6.97 ms、简化分离输出 6.78 ms；差异小于测量波动，不能据此宣称板端加速。
- 结果已保存：`evidence/host-onnx-320-optimization-20260905.json`。结论：继续做 ONNX 简化没有实质收益，下一步应聚焦 ESP-DL P4 内核命中率、内存访问和板端 `model.run()` 实测，而不是重复图简化。

##### [2026-09-05] ESP-DL 算子与量化路径静态核查（未刷写）
- 现有分离输出模型的图包含 64 个 Conv、57 个 Swish、15 个 RequantizeLinear、2 个 Resize，以及 YOLO 解码所需的 Sigmoid、Softmax、Concat、Split 等算子。
- ESP-DL 导出信息中相关节点均标记为 `quant_type='S8'`，量化调度为 `ESPDL_INT8`；未发现明显的浮点节点或缺失算子实现。
- 结论：当前主要瓶颈不能通过“补一个算子/改成 INT8”解决；最有价值的下一步是板端实测 `model.run()`、模型参数驻留位置、峰值连续内存和双核收益。稳定固件与模型分区均未改动。

##### [2026-09-05] ESP-DL 性能诊断接口核对（未刷写）
- 官方 `dl::Model` 提供 `profile_memory()` 和 `profile_module(true)`：前者可输出模型、参数副本、变量的内部内存/PSRAM/Flash 占用；后者可按模块耗时排序。
- 关键限制：官方 `get_module_info()` 在逐模块分析时固定调用 `forward(..., RUNTIME_MODE_SINGLE_CORE)`，因此模块表只能用于识别热点，不能与产品路径的 `run(RUNTIME_MODE_MULTI_CORE)` 总耗时直接比较。
- 后续实测将分离为两组：产品模式采集 `capture/preprocess/双核 model.run/decode` 总耗时；诊断模式仅采集一次内存表和单核热点表。诊断不得放在上电路径，避免影响稳定启动；目前未构建、未刷写。

##### [2026-09-05] ESP-DL 导出量化参数静态核查（未刷写）
- 导出 JSON 共记录 180 个调度节点、356 个量化值；节点调度均为 `ESPDL_INT8`。
- 检查到的 11,074 个量化 scale 全部为 2 的整数次幂（无非 2 次幂 scale），符合 P4 定点快速路径的预期。
- 因此当前模型不存在可通过“改成对齐量化 scale”获得的明显低风险收益；任何进一步收益需要板端 profiling 或重新训练/导出模型，不能只靠改配置推断。

##### [2026-09-05] ONNX 卷积热点报告（未刷写）
- 新增只读脚本 `tools/analyze_onnx_conv_hotspots.py`，对分离输出 ONNX 的 64 个卷积层计算理论 MAC 数和权重大小。
- 全图理论计算量约 5.19G MAC，卷积权重约 11.45 MiB；最大热点为 `/model.7/conv/Conv`，约 755M MAC，其次为 10×10 特征图上的 1×1 卷积。
- 报告已保存：`evidence/onnx-conv-hotspots-20260905.json`。这只是静态排序，不能替代板端耗时；后续 profiling 应优先关注这些层是否得到 P4 专用内核和双核拆分。

##### [2026-09-05] 按需 ESP-DL profiling 候选已构建（未刷写）
- 版本号：`2.0-profile`。后续所有候选和实机刷写记录均采用“主版本.次版本-用途”格式，并在刷写前明确版本号。
- 在产品推理路径中加入编译期开关 `ENERGY_LABEL_ENABLE_PROFILE`；默认值为 0，不改变稳定产品行为。
- profiling 打开时，仅在第一次 LABEL/缺陷检测动作完成后输出一次 `profile_memory()` 与 `profile_module(true)`；上电路径、UI、触摸、拍摄和模型分区均不变。
- `profile_memory()` 输出内存分布；`profile_module(true)` 输出单核算子热点，同时保留产品双核 `[PERF]` 计时。
- VM 编译通过，候选：`artifacts/candidate-profile-on-demand-20260905/mipi_isp_dsi.bin`，大小 2,944,320 字节，SHA-256 `3572d2663d25d353325cca68ef9fe7b4066fcdd642877c768aa41375d1ec9ae3`。
- 候选只允许写 `0x10000` 应用区，明确不写 `0x410000` 模型区；回滚包和写入说明已保存于同目录 `manifest.txt`。当前未刷写，等待实机测试窗口。

##### [2026-09-05] 版本 2.0-profile 已刷写，等待实机观察
- 已确认 `/dev/ttyACM0` 下载口可连接 ESP32-P4 v3.2；仅写入应用区 `0x10000`，未写 `0x2000`、`0x8000` 或模型区 `0x410000`。
- 写入候选：`artifacts/candidate-profile-on-demand-20260905/mipi_isp_dsi.bin`，SHA-256 `3572d2663d25d353325cca68ef9fe7b4066fcdd642877c768aa41375d1ec9ae3`，写入与 `verify-flash` 均通过，已执行硬复位。
- 当前暂停等待用户观察：启动是否亮屏、触摸是否正常、拍摄是否可用；随后点击一次标签或缺陷检测，观察等待页面和最终结果。首次检测还会输出一次 profiling 数据。
- 如出现黑屏或无法操作，立即使用既有恢复包回滚，不再继续刷写其他候选。

## ESP-DL 硬件加速 ABI 验证（2026-09-01）

- 状态：进行中，未刷写开发板、未访问 `0x2000`、`0xE0000` 或模型分区。
- 已验证：ESP-DL 应用算子、P4 汇编、官方 `libfbs_model.a` 与最小 FbsLoader 适配可以按 `rv32imafc / ilp32f` 编译。
- 新发现的系统级阻塞：当前 OpenVela ESP32-P4 底层 HAL（例如 `libarch.a` 中的 DSI、ISP、SPI、I2C 对象）仍按软浮点 ABI 编译；官方 ESP-DL 模型库是单精度硬浮点 ABI。最终链接明确拒绝混用：`can't link soft-float modules with single-float modules`。
- 结论：不能通过只补充 `libgcc` / `libstdc++` 解决；必须在隔离树中让底层 HAL 与整个系统统一按硬浮点 ABI 重新构建，并完成启动、显示和触摸回归后，才可能产生板端候选镜像。
- 证据：远程隔离目录 `/home/max/openvela-p4-espdl-probe-20260901/evidence/m15-espdl-probe-20260901-nuttx-hardf-link-closure.log`。

Last updated: 2026-08-31 (Chinese UI redeployed after board-state recovery; touch acceptance pending)

### Continuous UI live-preview physical acceptance (2026-08-31)

- The frame-pump candidate `artifacts/ui-two-tap-live-preview-pump-20260831.bin`
  is now physically accepted for its first interaction: after one `CAPTURE`
  touch and release, it stays in the viewfinder and the camera picture changes
  continuously as the target moves. This confirms that the UI-integrated loop
  is continually driving `esp32p4_csi_capture_one_rgb565()` rather than
  presenting a single cached frame.
- The deployed image is 437,672 bytes, SHA-256
  `942343dffa8559fcce468a82ceec55962a77215009918d9af87c81a9fa7d1a3f`.
  It was written and independently verified at application offset `0x2000`
  only. The protected model container at `0xE0000` was not accessed.
- Remaining acceptance for this state is intentionally narrow: a second
  `CAPTURE` touch must retain the current RGB565 frame, return to the normal
  UI, show `FRAME READY`, and leave touch/buttons usable. No model inference
  is involved in this check.

#### Second-tap capture acceptance (2026-08-31)

- Physical result: accepted. The second `CAPTURE` touch exits the continuous
  viewfinder, retains the current frame, and returns to the normal UI with
  `FRAME READY` displayed. The requested two-tap camera workflow is now
  complete: first touch opens persistent live framing; second touch captures.
- Next regression check: `GALLERY` must still show that captured frame and
  return to the normal UI, after which `LABEL` can be validated against the
  frame produced by the new live-preview route.

#### Live-capture gallery regression acceptance (2026-08-31)

- Physical result: accepted. After a frame is confirmed from the continuous
  viewfinder, `GALLERY` displays that captured image and returns to the
  normal UI. The live-preview state therefore preserves the captured RGB565
  buffer in the form expected by the existing gallery path.
- Next acceptance: press `LABEL` from `FRAME READY` and observe the visible
  processing state and final return/result. This verifies the handoff from a
  live-confirmed frame to the model entry point; it does not yet constitute
  acceptance of detection accuracy.

### LABEL static processing-page candidate (2026-08-31)

- The observed black screen during `LABEL` had a direct code cause: the prior
  callback deliberately called `esp32p4_dsi_status_ui_stop()` before its long,
  synchronous TFLite Micro operation. It was an intentional display shutdown,
  not evidence that the board had frozen.
- New deployed candidate: `artifacts/ui-label-static-wait-20260831.bin`,
  437,844 bytes, SHA-256
  `1eab87e2723132f097bd1f46a6fa6a7558377399eac136f2a65500383b942450`.
  `LABEL` now replaces the normal UI with a static `PROCESSING` page including
  `RUNNING ON DEVICE - PLEASE WAIT`; DSI remains active for the whole model
  operation. No camera/preview session is active during inference.
- Full offline build completed. Temporary remote source/configuration edits
  were restored. Boot-equivalence against the accepted live-preview image
  passed. The image was written to `0x2000` only; write-time hash validation
  and independent `verify-flash` passed. `0xE0000` was not accessed.
- Pending physical acceptance: after live capture returns `FRAME READY`, press
  `LABEL`; the static processing page must remain visible throughout
  computation and the board must return to a usable result UI afterward.

### Chinese UI candidate (2026-08-31)

- Per user request, all visible UI copy except the brand `OPENVELA` and the
  hardware identifier `ESP32-P4` has been translated to Chinese. This covers
  the normal status UI, capture/gallery/label/inspection buttons, capture
  readiness states, and the static inference waiting page.
- Because the existing renderer only supported an ASCII 5×7 font, the image
  adds a compact board-side 16×16 bitmap subset for exactly the Chinese
  characters used by the UI; this avoids blank or garbled replacement text.
- Deployed candidate: `artifacts/ui-chinese-20260831.bin`, 438,372 bytes,
  SHA-256 `dcdeb3bd92102b7da3e1b5451f9b00c93e8473247d23f09ef715ce71b00eeccd`.
  Full offline build completed; boot-equivalence against the static-wait
  candidate passed. It was written only to `0x2000`, with both write-time hash
  validation and independent `verify-flash` passing. `0xE0000` was not
  accessed.
- Pending physical acceptance: confirm Chinese text is legible, buttons still
  operate, and the two-tap live preview/capture path remains intact.

#### Physical rejection and rollback (2026-08-31)

- Physical result: rejected. The Chinese bitmap-font version booted but touch
  buttons became non-responsive. Startup-shape equivalence was insufficient to
  protect the time-sensitive DSI/GT911 runtime path after the renderer grew.
- Immediate recovery completed: `ui-label-static-wait-20260831.bin` was
  restored to `0x2000` only. Both write-time validation and independent
  `verify-flash` passed; `0xE0000` was not accessed.
- Freeze rule: do not deploy another Chinese renderer/font implementation that
  changes the linked DSI source path without a physical touch regression test.
  The next localization route must preserve the accepted runtime layout or use
  a separately tested display layer.

#### Recovery correction (2026-08-31)

- The static-wait image was not itself physically accepted for touch. User
  retest confirmed it also left the buttons non-responsive, so it is rejected
  as a recovery baseline.
- The board is now restored to the physically accepted frame-pump image
  `ui-two-tap-live-preview-pump-20260831.bin`, SHA-256
  `942343dffa8559fcce468a82ceec55962a77215009918d9af87c81a9fa7d1a3f`.
  It was written and independently verified at `0x2000` only; `0xE0000` was
  not accessed. This is the only current candidate with accepted continuous
  preview, two-tap capture, gallery return, and touch behavior.

#### Touch recovery escalation (2026-08-31)

- User reported that the frame-pump image also remained non-responsive after
  a verified reflash. This means the symptom can no longer be attributed only
  to the Chinese rendering candidate.
- Local UART0 `COM12` was observed at 115200 for eight seconds after reset;
  it produced zero bytes, so there is currently no serial evidence that the
  GT911 polling loop has reached its active state.
- Recovery action: the original long-term physical UI/touch baseline
  `artifacts/ui-only-touch-press-release-20260821/nuttx-ui-only-touch-press-release-20260821.bin`
  was written only at `0x2000`. Write-time hash validation and independent
  `verify-flash` passed. The model container at `0xE0000` was not accessed.
- Current acceptance condition is deliberately reduced: verify that this
  frozen baseline registers press-darken/release-restore feedback before any
  camera, live-preview, waiting-page, or localization image is redeployed.

#### Cold-start blocker (2026-08-31)

- The frozen UI/touch baseline was also physically non-responsive after a
  verified application-only reflash. This rules out the recently added Chinese
  UI, static waiting page, and live-preview candidates as sufficient causes.
- Board documentation records that GT911 has no dedicated RST/INT GPIO on this
  hardware; it is a polled I2C device on GPIO8/7. A tool-driven RTS reset is
  therefore not an independent GT911 power reset. In addition, repeated
  flashing after which `COM12` produces no startup bytes is an observed board
  condition for which historical guidance requires a physical USB power cycle.
- No further software flash/retry is justified until the board receives a
  full cold start: unplug its USB power/data cable for at least ten seconds,
  reconnect it, wait for the UI, then test the frozen baseline's press/release
  feedback. If it remains unresponsive after that, the next work is hardware
  I2C/FFC/power diagnosis rather than another firmware change.

#### Board-state recovery and Chinese UI redeployment (2026-08-31)

- User reported that a renewed board check restored normal behavior. The prior
  non-responsive interval is therefore recorded as an unresolved transient
  board/connection state, not conclusive evidence that the Chinese font image
  caused the fault.
- At the user's request, the Chinese UI candidate
  `artifacts/ui-chinese-20260831.bin` (SHA-256
  `dcdeb3bd92102b7da3e1b5451f9b00c93e8473247d23f09ef715ce71b00eeccd`) was
  redeployed. It was written only to `0x2000`; write-time validation and
  independent `verify-flash` passed. `0xE0000` was not accessed.
- Pending acceptance: Chinese copy must render correctly and normal touch
  feedback/controls must remain responsive after boot.

### Single-session low-FPS viewfinder deployment (2026-08-31)

- The two earlier low-FPS candidates were physically rejected because they
  showed only one frame before returning to the UI.  The second rejection
  proved this is not caused by an initiating-touch confirmation event.
- A new candidate keeps SC2336, CSI, and ISP initialized for the complete
  viewfinder session.  It time-shares CSI capture and DSI presentation for
  every frame; it does not reinitialize the camera for the second frame.
- Candidate: `artifacts/ui-lowfps-viewfinder-session-20260831.bin`, 438,200
  bytes, SHA-256 `9b320afa9bac7e6ec53517e0585d57fb61ecbb9f82f859306d0346042822d273`.
  Offline full build completed; temporary remote source/configuration edits
  were restored; boot-equivalence against the accepted capture-review image
  passed.
- The candidate was deployed on 2026-08-31 to application offset `0x2000`
  only. Both write-time hash validation and a separate `verify-flash` passed.
  The protected external model container at `0xE0000` was not accessed.
- Pending physical acceptance: one `CAPTURE` press must visibly produce
  multiple changing frames while the target moves, and must not auto-return
  to the UI. A subsequent press must save the last frame and restore
  `FRAME READY` with the accepted UI/touch behavior intact.

#### Physical result and recovery (2026-08-31)

- Physical result: the single-session candidate still displayed one frame
  only.  It is rejected.  This eliminates both per-frame camera
  reinitialization and initiating-touch confirmation as explanations.
- Strongest current diagnosis: after the first CSI capture, releasing CSI's
  bridge/DMA path so DSI can present the frame cannot be resumed inside the
  same ISP session.  Repeating bridge enable/disable is therefore not a
  viable viewfinder mechanism on this hardware/software path.
- Recovery completed immediately: the accepted
  `ui-capture-review-20260831.bin` was restored at `0x2000` only.  Write-time
  validation and an independent `verify-flash` both passed.  No operation
  accessed `0xE0000`.
- Do not build or flash further variants of either per-frame CSI restart or
  CSI-bridge toggle viewfinders.  A true continuous preview now requires a
  lower-level DMA/display ownership design with observability, or a separate
  camera data route.  Until that exists, the accepted three-second single
  frame composition review remains the safe camera workflow.

#### Correction: prior continuous-preview evidence (2026-08-31)

- The statement that the board path cannot provide a real-time picture was
  too broad and is withdrawn.  The user correctly recalled an earlier
  successful test: `docs/handoffs/11-l1825-official-full-camera-display-handoff-20260814.md`
  records the official ESP-IDF `mipi_isp_dsi` full example as continuously
  displaying changing SC2336 camera frames.
- This is not equivalent to the current UI's stop-DSI / capture / restart-DSI
  implementation.  The preserved NuttX live implementation also shows the
  required architecture: CSI channel 0 and DSI channel 1 run together, CSI
  owns `DW_GDMA_INTR_SOURCE`, and its ISR explicitly calls
  `esp32p4_dsi_gdma_irq_handler()` to re-arm DSI channel 1.  That exact
  shared-interrupt route is absent from both rejected time-sliced candidates.
- Next work is therefore a narrow reconstruction of this already-existing
  simultaneous live path behind CAPTURE, with an explicit observability
  counter for CSI frames and DSI re-arms.  It must not be replaced by more
  CSI/DSI stop-start experiments.

#### Two-tap live-preview release guard (2026-08-31)

- Review of the previously rejected simultaneous-preview candidate found a
  concrete state-machine defect: the release of the same CAPTURE touch that
  starts preview was accepted as the second confirmation, immediately
  stopping preview.  That candidate therefore did not implement the specified
  two-tap interaction.
- New candidate `artifacts/ui-two-tap-live-preview-armed-20260831.bin`
  (437,588 bytes, SHA-256
  `a85577842bdce76b4810494881b4aeadcd01579c974ed1ce7518554455398964`)
  adds only a release guard: first release arms confirmation; the next
  press/release saves the frame and restores `FRAME READY`.  The CSI/DSI
  simultaneous shared-GDMA route is unchanged.
- Full offline build completed, temporary remote source and config changes
  were restored, and boot-equivalence against the accepted capture-review
  image passed.  The candidate was written and independently verified at
  application offset `0x2000` only; no operation accessed `0xE0000`.
- Pending physical test: press CAPTURE once, release, then leave the display
  untouched while moving a target.  It must remain visibly live.  Only a
  later second touch may return to the UI.

#### Physical rejection and recovery (2026-08-31)

- Physical result: the release-guard candidate displayed a black screen on
  the first CAPTURE action. It is rejected. The release guard was a real
  state-machine correction, but it did not solve the lower-level live-display
  failure.
- The accepted `ui-capture-review-20260831.bin` was restored immediately to
  `0x2000` only. Write-time validation and a separate `verify-flash` passed;
  `0xE0000` was not accessed.
- No more direct UI integration variants of the current NuttX simultaneous
  CSI/DSI implementation may be flashed. The remaining discrepancy is at the
  DMA/interrupt/panel-session layer and must first be isolated outside the
  status UI, with a reproducible observability path. The official ESP-IDF
  full example remains evidence that hardware can live-preview; it does not
  validate this NuttX UI integration.

#### Isolated NuttX live-path reproduction (2026-08-31)

- The preserved isolated NuttX stability image
  `.codex/isp-dsi-preview/nuttx-l1825f-isp-dsi-stability.bin` was re-run on
  this board as a bounded diagnostic. Its SHA-256 is
  `563e7f70d6babef87afc0ca0d88ef73acb7c88b0e3bc18a1cf9e740aaa8b737e`.
  It was written and independently verified at `0x2000` only.
- UART0 on local `COM12` captured a fresh successful 60-second run:
  `live stability completed frames=900 elapsed_ms=60000`. The one-second
  heartbeat advances by 15 frames per second, so the isolated NuttX
  CSI+ISP+DSI path is currently proven live at approximately 15 FPS.
- This confirms the physical camera, display, CSI, ISP, shared GDMA interrupt
  route, and the isolated NuttX live implementation all work. The failure
  boundary is specifically integration with the persistent UI/GT911 state
  machine and its DSI ownership lifecycle.
- Immediately after the test, accepted
  `ui-capture-review-20260831.bin` was restored and separately
  `verify-flash`-validated at `0x2000`. The model container at `0xE0000` was
  not accessed. Evidence log:
  `.codex/isp-dsi-preview/isp-dsi-stability-rerun-20260831.txt`.

This is the durable progress record for the ESP32-P4 OpenVela defect-detection
terminal. Each module has one current status, a percentage, evidence, and a
next acceptance condition. A completed module is frozen unless a later change
explicitly requires it; model work must not replace the validated UI/touch
baseline.

**This document is the single source of truth for project progress.** It covers
every planned project module in the Module Register. No module may be treated
as complete, pending, blocked, or in progress solely from conversational
memory; its current row in this file is authoritative.

## Low-FPS Alignment Viewfinder Candidate (2026-08-31)

- `CAPTURE` is changed into a state-machine request: DSI stops, one RGB565
  frame is captured, a temporary cyan alignment frame/centre cross is shown
  for 250 ms, then DSI stops before the next capture. A second touch saves a
  newly captured clean frame and returns to `FRAME READY`; guide pixels never
  enter the saved model input.
- Offline candidate: `artifacts/ui-lowfps-viewfinder-20260831.bin` (437,752
  bytes), SHA-256 `0485a00c5b7b17049ea4b0bf6a3e39dd640c41b54bcc1e54ac8f60f6d85b25d2`.
  Boot-equivalence against the accepted capture-review image: `PASS`.
- The reversible probe wrote and verified only `0x2000`, but startup log
  capture could not run because VM device `/dev/ttyUSB0` is absent. Its trap
  restored `ui-capture-review-20260831.bin`; full readback reported
  `restore_BYTE_EXACT=PASS`. `0xE0000` was not accessed.
- Status: **not deployed for visual acceptance**; board has the accepted
  capture-review image. Deploy only after UART0 passthrough is restored or
  after explicit approval for a visual-only deployment.

### Physical result and rollback (2026-08-31)

- Visual-only deployment was explicitly approved and completed with an
  independent `verify-flash` pass at application offset `0x2000` only.
- Physical result: first `CAPTURE` showed one camera frame and immediately
  returned to UI; it was **not** a continuously updating alignment viewfinder.
  This candidate is rejected for the requested workflow.
- The accepted capture-review image was immediately restored to `0x2000` and
  independently verified. The next candidate must consume the initiating
  button release before it permits a confirmation touch, and must expose a
  visible multi-frame heartbeat so one-frame failure and premature confirmation
  are distinguishable during physical testing.

## Current Model-UI Integration Gate (2026-08-31)

- The accepted board application remains
  `artifacts/ui-label-gate-20260831.bin`, written only at `0x2000`, SHA-256
  `2135da9d914d6dc39701068aabc4d4c04e4ec53837ea7835c6a0ed3d402e5dc1`.
  Its persistent UI, press-darken/release-restore feedback, safe one-frame
  capture, gallery preview, and `LABEL MODEL PENDING` gate were physically
  accepted. The external model container at `0xE0000` was not accessed.
- An offline linker proof completed successfully:
  `/tmp/openvela-tflm-linkproof-20260831.bin` (433,772 bytes, SHA-256
  `b5a149a7f6026d16c3b0bb57d318fc4644e35d4a403c10c28a04d4e40fa16f11`).
  Its ELF contains `camera_diag_tflm_init_diag`, the external-model PSRAM
  loader, and `tflite::MicroInterpreter` symbols. This proves the selected
  TFLite Micro runtime can link in this OpenVela build; it was not flashed.
- The next offline candidate is
  `/tmp/openvela-label-tflm-candidate-20260831.bin` (436,572 bytes, SHA-256
  `aabb240222e357c87d8fb7056ce75c0ce3fb00d859ad11db6446c9594b15b8f4`).
  `LABEL` is wired to stop DSI, invoke the existing 256×256 external TFLM
  model on the captured RGB565 frame, then restart DSI and show label-found,
  energy-level, and defect-count data. Static symbols prove that exact call
  chain is retained. The build script contains no flash write, erase, or
  `0xE0000` operation, and its temporary source/config edits were restored.
- This candidate is **not yet eligible for a blind flash**: the historical
  real-frame Invoke cost is about 112 seconds and DSI must stay stopped during
  that interval. Before board validation, prepare a reversible one-shot
  `0x2000` probe with UART capture and automatic rollback to the accepted
  `ui-label-gate` image. Model area `0xE0000` remains read-only.

### Annotated result image and processing notice (2026-08-31)

- Candidate `artifacts/ui-label-tflm-annotated-20260831.bin` added actual
  model-result rendering: after real Invoke, accepted NMS boxes are drawn
  directly onto the captured RGB565 PSRAM frame; `GALLERY` therefore shows
  the annotated image, not a synthetic overlay. Colors are red for defects,
  green for energy-level candidates, cyan for label candidates, and yellow
  for other structural candidates. It was boot-probed with automatic rollback
  and then superseded by the processing-notice variant.
- Current deployed application is
  `artifacts/ui-label-tflm-processing-20260831.bin`, 437,344 bytes, SHA-256
  `1945226d262cefaf729ec6c0252362b256121b8048420270539ca54a39794733`.
  Before the deliberately exclusive Invoke phase, `LABEL` now visibly shows
  `INFERENCE STARTING - SCREEN WILL PAUSE` for two seconds. Only then does it
  stop DSI for the approximately two-minute model invocation, and it restores
  the UI/result image afterward. This does not claim display during inference.
- The processing candidate passed a reversible boot probe: NuttX, NSH, DSI
  UI, and GT911 signatures were all present. The rollback rewrote only
  `0x2000`, with byte-exact restoration of the 302,020-byte accepted
  `ui-label-gate` image. The deployed write and independent verify of the
  processing candidate also passed. `0xE0000` was not written.

### CAPTURE 单帧构图回看（2026-08-31）

- 为满足“拍照时可确认构图”的需求，新增候选
  `artifacts/ui-capture-review-20260831.bin`（437,376 字节，SHA-256
  `8051b8d74c59b0c44d2cf0a3ae4268a2785e1fbeb5252dcf8165e109c85d251d`）。
  `CAPTURE` 的安全顺序为：停止 DSI → CSI/ISP 采一张 RGB565 → 全屏回看约三秒
  → 恢复 UI；用户可以反复拍摄并调整物体位置，满意后再点 `LABEL`。
- 这是刻意选择的单帧回看，不是 DSI 与 CSI/ISP 同时工作的实时预览；后者曾出现
  蓝屏/黑屏风险。候选静态审计确认含采集、真实 TFLM Invoke、标注图绘制符号，且
  临时构建修改已恢复；构建脚本不含 `write-flash`、`erase-flash` 或 `0xE0000`。
- 自动回滚探针已确认候选写入及校验成功，并将稳定镜像恢复为逐字节一致；该次因
  虚拟机未透传 `/dev/ttyUSB0`，未能采到 UART 启动指纹。随后候选仅写入 `0x2000`
  并经独立 `verify-flash` 通过；模型分区 `0xE0000` 未写入。待实屏验收构图回看、
  自动回 UI 与触摸持续性。

### CAPTURE 两次点击实时预览候选（2026-08-31）

- 按用户确认的交互改为：第一次点击 `CAPTURE` 停止状态 UI 并启动 CSI/ISP→DSI
  实时预览；第二次触摸在保持 GT911 轮询的同时停止预览、保留当前 RGB565 帧并
  恢复状态 UI。预览中不执行推理，确认后可继续 `LABEL`。
- 候选 `artifacts/ui-two-tap-live-preview-20260831.bin` 已完成离线全量构建，
  437,536 字节，SHA-256
  `4672a690631507e93aaf7123d55a86d5dcaf8d97e5207687c1c6e1357065684b`。静态符号
  含 `camera_diag_ui_preview_toggle_for_ui`、`esp32p4_dsi_live_start/stop`、
  真实 TFLM Invoke；临时源和 `.config` 已恢复，构建脚本没有刷写、擦除或访问
  `0xE0000`。
- 可回滚探针已完成候选应用区写入和校验，并恢复上一版
  `ui-capture-review` 镜像逐字节一致；由于虚拟机未透传 `/dev/ttyUSB0`，没有 UART
  启动指纹。随后仅将候选写入 `0x2000` 并独立 `verify-flash` 通过，模型区
  `0xE0000` 未写入。待用户实屏验收“第一次进入实时画面、第二次确认回 UI、触摸持续”。
- **实屏结果：失败。** 用户实际点击第一次 `CAPTURE` 后实时画面为黑色，故该候选
  不得作为可用功能继续保留。已立即恢复到
  `ui-capture-review-20260831.bin`（SHA-256
  `8051b8d74c59b0c44d2cf0a3ae4268a2785e1fbeb5252dcf8165e109c85d251d`），
  只写 `0x2000`，独立 `verify-flash` 通过；`0xE0000` 未访问。
- 当前结论：现有共享 GDMA 实时通路虽然能链接，不能替代实屏数据面验收。继续排查前
  必须先恢复可读取的 UART0（VM 当前缺失 `/dev/ttyUSB0`）或建立其他可重复的帧/显示
  观测证据；不得再盲目重复部署该实时候选。

### Module Register (current)

| Module | Status | Progress | Current acceptance condition |
| --- | --- | ---: | --- |
| Stable display and touch | Complete | 100% | Persistent UI and press/release feedback accepted on board. |
| Real button and flow binding | In progress | 94% | Validate real LABEL invocation without regressing accepted buttons. |
| Camera acquisition | In progress | 70% | Accept the repeatable three-second composition review and target framing. |
| On-board model integration | In progress | 74% | Reversible physical run of the linked real-frame Invoke path. |
| Defect workflow and result annotation | In progress | 30% | Accept annotated result image and complete the full defect workflow. |
| Whole-device acceptance | In progress | 38% | End-to-end capture, inference, results, stability, and recovery evidence. |

Status values:

- Complete: accepted on the physical board or otherwise fully verified.
- In progress: implementation or board validation remains.
- Pending: not started because an earlier dependency remains.
- Blocked: cannot proceed until a stated external condition changes.

## Frozen Board Baseline

| Field | Value |
| --- | --- |
| Board | ESP32-P4, UART0 `COM12` |
| Application Flash offset | `0x2000` |
| External model-container offset | `0xE0000` |
| Current image | `artifacts/ui-only-touch-press-release-20260821/nuttx-ui-only-touch-press-release-20260821.bin` |
| Image size | 300,172 bytes |
| SHA-256 | `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743` |
| Model in boot image | No. The app excludes embedded `label_model_int8`, `label_inspection_demo`, and TFLite Micro; the validated model container is stored at `0xE0000`. |
| Freeze rule | Do not replace the validated UI/touch startup path to debug model loading, preprocessing, inference, or postprocessing. |

## Tail-Section Extension Audit (2026-08-30)

- A deliberately minimal tail-section probe was built from the exactly
  reproducible stable source tree.  It added one four-byte tail hook and
  redirected only the existing touch-release call to that hook; the hook
  immediately jumps back to the original release function.
- The candidate did preserve the symbol addresses of the principal DSI and
  touch functions, but byte-level comparison rejected it: 17,931 shared
  `.flash.text` bytes differ across 424 ranges.  `touch_diag` itself changed
  at the release call and at two read-only-data address materializations.
- Disassembly showed the latter change is caused by a DROM layout shift
  (`0x40039020` to `0x40039168`) induced by the extra linked text.  Therefore
  unchanged symbol addresses and unchanged RAM segment sizes are insufficient
  evidence of display-path equivalence.
- **Decision:** the tail-section route is not eligible for board flashing.
  No Flash write was performed and model container `0xE0000` was not read or
  written.  The accepted UI/touch image remains the only board baseline.
- The only viable route to actual button/camera/model functionality is a new
  recoverable system image designed and validated as a complete boot path;
  it must be evaluated separately, with automatic rollback to the frozen
  baseline after failed UART or screen/touch acceptance.

## Automatic-Startup Reproduction Candidate (2026-08-30 continuation)

- Retrieved the independently built candidate from
  `/home/max/openvela-p4-ui-autostart-repro-20260829` as
  `artifacts/ui-autostart-repro-20260829.bin` (SHA-256
  `cfca93d49a751fb08e75edd80e2c253f737b5aa2bf3d730706ffcd76ea4d0c59`).
- Local `tools/verify_esp32p4_ui_boot_equivalence.py` returned `PASS`: image
  header, entry point, RAM segment addresses, and RAM segment lengths match
  the frozen 300,172-byte baseline.  This is a static gate only; it is not a
  physical-board acceptance and it does not authorize replacing the current
  board image.
- This candidate is the first controlled artifact suitable for a single
  hardware startup probe because it restores the verified automatic
  `openvela_ui` startup path without adding workflow/model code.  Before any
  write, the rollback image, UART capture command, and one-shot screen/touch
  acceptance checklist must be prepared. The protected model container at
  `0xE0000` remains untouched.

### One-shot probe result (2026-08-30)

- The candidate was written only at `0x2000`; esptool write-time verification
  succeeded.  The intended UART capture then failed before opening the
  console because the referenced VM path did not contain
  `tools/capture_openvela_boot_uart.py`.
- The trap immediately restored the frozen image and verified it by both
  esptool and a 300,172-byte readback.  Readback SHA-256 matched the baseline
  exactly: `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`.
- **No boot/display/touch conclusion is drawn from this attempt.**  The next
  action is to upload the capture helper into the VM, rerun the same one-shot
  probe, and retain the automatic rollback.

### One-shot probe rerun (2026-08-30)

- The UART helper was uploaded to the VM and the candidate was tested once
  under the rollback trap. Flash write and verify at `0x2000` succeeded.
- UART evidence: `reset_exit=0`, `booting_nuttx=1`, `nsh=1`, but
  `dsi_active=0` and `gt911=0`. The console reached `nsh>` and printed the
  diagnostic usage text; automatic UI startup was not observed. This is a
  failed candidate acceptance, not a display/touch pass.
- Automatic rollback rewrote only `0x2000`; the 300,172-byte readback matched
  the frozen SHA-256 exactly (`22e48e28...e743`). `0xE0000` was untouched.
- **Decision:** do not reuse this candidate. The next build must correct the
  startup command/task path and repeat UART evidence before any screen test.

## 320px Target-Allocation Isolation (2026-08-30)

- The earlier 841,028-byte allocation candidate was rejected before any
  further device write: it reached only early boot in the previous physical
  test, before NSH.  Its `camera_diag` entry still retained historical
  ESP-DL and validation-sample references, so it was not an isolated TFLite
  allocation test.
- The physical board was restored and UART-verified on the frozen UI/touch
  image (`22e48e28...e743`) and the original external model container
  (`38f2915a...ad3`).  Do not use the 841,028-byte image again.
- A separate remote build tree now contains a true-minimal candidate: its
  only ROMFS action is `camera_diag --tflm-init`; its application sources are
  limited to the external-model reader, SHA-256, and the TFLite allocation
  diagnostic.  No display, touch, CSI/ISP, ESP-DL, invoke, or validation
  sample code is deliberately selected.  A clean full rebuild is in progress
  to remove stale static-library members before the binary size and symbols
  are accepted.  No board Flash operation has occurred during this work.

### Physical Allocation Result and Mandatory Recovery (2026-08-30)

- The clean isolated candidate built successfully as 353,672 bytes,
  SHA-256 `eb207d8d6bbb1ca83682e5c847e515994ec8830805b2839d783b9fa2b2944804`.
  Static audit passed: its startup script contains only
  `camera_diag --tflm-init`; no DSI, GT911, CSI/ISP, ESP-DL, Invoke, or
  validation-sample symbols were present.
- A single controlled physical validation wrote the candidate at `0x2000` and
  the 320px OVM10 container at `0xE0000`. UART proved the actual target path:
  `AllocateTensors status=0 elapsed_ms=20 arena_used=1314236`, input
  `1x320x320x3 int8`, output `1x14x2100 int8`.
- The test did not call `Invoke()` and did not initialize display, touch or
  camera. Immediately after UART capture, both protected regions were
  restored using preserved Flash settings. Independent readback and `cmp`
  verification passed byte-for-byte for the stable UI/touch application
  (`22e48e28...e743`, 300,172 bytes) and original model container
  (`38f2915a...ad3`, 3,244,032 bytes). The board is therefore back on the
  frozen baseline. Future 320px work may start from this allocation proof but
  must remain a separately reversible candidate.

### 320px Zero-Input Invoke Attempt and Recovery (2026-08-30)

- A second clean isolated candidate was built only to fill the 320px INT8
  input tensor with its zero-point and call `MicroInterpreter::Invoke()` once.
  It contains no DSI, GT911, CSI/ISP, ESP-DL, validation images, or workflow
  code. Its ROMFS command is only `camera_diag --tflm-invoke-zero`; static
  symbol audit confirmed `camera_diag_tflm_invoke_zero_diag`,
  `MicroInterpreter::Invoke`, and the external-model reader. The candidate is
  354,580 bytes, SHA-256 `1c513ae99aec8c73bf6162714e634b3f53a28a2602387a146a8b4478bcc35523`.
- Candidate application and 320px OVM10 model writes both passed esptool
  write-time verification and a separate `verify-flash` pass. UART0 produced
  no bytes during the five-minute capture window, so **no successful Invoke is
  claimed** and no latency/result is recorded.
- The `finally` recovery then rewrote the frozen application and the original
  model container with preserved Flash parameters. Independent exact-length
  readback plus byte comparison passed for application SHA-256
  `22e48e28...e743` (300,172 bytes) and model-container SHA-256
  `38f2915a...ad3` (3,244,032 bytes). The board is restored to the frozen
  UI/touch baseline. The next model task is to repair the diagnostic boot/UART
  capture path before retrying Invoke; it must remain isolated and reversible.

### 320px Candidate Startup Diagnosis (2026-08-30)

- The UART capture mechanism itself is now verified: opening UART0 before an
  esptool reset on the restored stable image captured 3,594 bytes and all four
  startup fingerprints (NuttX, NSH, persistent DSI UI, GT911).
- Repeating the 320px candidate test with that exact known-good capture order
  produced the candidate ROM and `*** Booting NuttX ***` plus all seven loader
  segment lines, but stopped before NSH. Thus the failed Invoke result is not
  a serial-port/capture mistake and is not evidence of a TFLM operator fault:
  the rebuilt candidate has an early-startup failure before its ROMFS command.
- The wait was stopped as soon as that distinction was proven. Since this
  forced stop bypassed the original script's `finally`, an independent restore
  procedure immediately rewrote both frozen regions. `verify-flash`, exact
  readback SHA and byte comparison again passed for both the UI/touch app and
  model container (`FORCED_RECOVERY=PASS`).

### 320px Pre-Invoke Build Reproducibility Audit (2026-08-30)

- The isolated source files were restored exactly to the saved pre-Invoke
  versions, then two independent `make clean && make -j4` builds were run
  without any intervening source or configuration change. Both produced a
  353,672-byte `nuttx.bin`.
- Their SHA-256 values differ only because the embedded dirty-build timestamp
  differs. A byte comparison found exactly five changed bytes, all within that
  timestamp text; code, segment layout, and all remaining bytes were identical.
  Therefore the current isolated build is layout-reproducible, and the earlier
  historical-hash mismatch cannot be used as evidence that `Invoke()` alone
  caused the pre-NSH startup stop.
- No candidate was written to the board during this audit. The board remains
  on the frozen UI/touch application and original model container. The next
  diagnostic is a smallest-possible, reproducible source increment with a
  static startup/segment audit before any reversible physical retry.

### 320px Minimal Invoke Layout Audit (2026-08-30)

- Reapplying only the saved `--tflm-invoke-zero` command branch and its single
  diagnostic function produced a 354,580-byte candidate (an increase of 908
  bytes). Its ESP32-P4 entry point remains `0x4ff48754`; all three RAM-loaded
  segment addresses and lengths are identical to the 353,672-byte allocation
  baseline (`0x30100000/0x68`, `0x4ff40000/0x8aac`,
  `0x4ff48b00/0x1458`).
- The added function is 0x296 bytes at `0x4001eeda`; it shifts the existing
  allocation diagnostic to `0x4001f170`, but it does not alter the startup
  entry or RAM segment boundaries. This rules out a simple RAM-segment growth
  or entry-point change as the cause of the earlier pre-NSH stop. It does not
  yet prove the candidate is safe to flash.
- No board Flash operation was performed. Further work remains an offline
  placement/initialization audit followed only by a reversible, UART-captured
  board retry with automatic verified restoration.

### 320px Boot-Container Control Tests (2026-08-30)

- The recovery harness was strengthened so every test restores both the frozen
  application and original model container in an EXIT handler, then performs
  independent `verify-flash`, exact-length readback, SHA-256, and byte-for-byte
  comparisons. Both controlled attempts completed recovery with
  `app_BYTE_EXACT=PASS` and `model_BYTE_EXACT=PASS`.
- The current simple-boot candidate stops after the seven NuttX loader segment
  lines, before NSH. Its ROM message reports an all-zero expected image SHA.
  This is **not** by itself causal: the accepted frozen UI image emits the
  same all-zero expected-SHA message and then reaches NSH, DSI, and GT911.
- A control image made by removing `--ram-only-header` passed host-side
  validation-hash checking, but it is incompatible with this board's
  `CONFIG_ESPRESSIF_SIMPLE_BOOT` path: ROM tries to load an added padding
  segment at address zero and faults before NuttX. It was rejected and
  automatically restored. Do not use full-header packaging for this target.
- Therefore neither the 16MB-versus-4MB header field nor the all-zero ROM SHA
  message explains the current early stop. The remaining task is an offline
  provenance and binary-layout comparison against the previously proven
  353,672-byte allocation candidate. No further candidate may be flashed
  until that comparison identifies a meaningful difference.

### Historical M11 Startup Control (2026-08-30)

- Provenance correction: the separately archived control is 435,504 bytes /
  SHA-256 `38fc16c3f1535b8e14f39683a1cde5ac7a286b4293ece2b87394ab1d2360ee1e`.
  It is only a legacy-format observation, not a substitute for accepted M11
  evidence.  A second 435,012-byte image was now recovered from the immutable
  `evidence/openvela-flash-audit-low-20260822.bin` at its recorded app offset
  `0x2000`: SHA-256 `ff8e3a65c4d69e58be4584d0c5e821582561b930ed2c8d9e0278be664363e1c5`.
  It parses as a valid 4MB/DIO/80MHz simple-boot image and embeds the
  2026-08-22 `camera_diag --tflm-init` diagnostic strings. This differs from
  the previously recorded `9bb42d217...e1c` hash, so its historical acceptance
  provenance is still unconfirmed; it may be used only as a reversible
  boot-only control until UART verification establishes its behavior.

- A known historical 256px TFLite Micro initialization image was tested as a
  control. It is an independently archived 435,504-byte, 4MB/DIO/80MHz
  simple-boot image that previously reached `camera_diag --tflm-init` and
  completed real `AllocateTensors()` on this project.
- In the current board environment it reproduced the exact same early-start
  symptom as the 320px isolated candidate: ROM loaded all seven segments and
  NuttX printed its segment table, but NSH never appeared. No model command
  was invoked. This proves the current stop is not specific to the 320px
  model, its TFLite resolver, the zero-input `Invoke()` code, or the current
  candidate's source size.
- The control test altered only `0x2000`; it did not access the model region.
  Its automatic restoration passed byte-for-byte, and a final stable-image
  UART capture again confirmed `booting_nuttx=1`, `nsh=1`, `dsi_active=1`, and
  `gt911=1`. Further model work must not spend more board writes on these
  legacy diagnostic images; it needs a loadable extension architecture from
  the frozen stable system.
- The 435,012-byte image recovered directly from the 2026-08-22 low-Flash
  audit was then tested under the same reversible harness. It also reached
  `*** Booting NuttX ***` and all seven ROM/NuttX loader segments but stopped
  before NSH; no diagnostic command ran. The application-only automatic
  recovery read back SHA-256 `22e48e28...e743` and
  `app_BYTE_EXACT=PASS`. A separate post-recovery UART capture returned
  `booting_nuttx=1`, `nsh=1`, `dsi_active=1`, `gt911=1`. This strengthens the
  conclusion that the early stop is a legacy diagnostic boot-layout problem,
  not a 320px model, `Invoke()`, or model-container issue. Do not flash more
  legacy diagnostic candidates without a new differentiating hypothesis.

### Frozen UI Extension Architecture Audit (2026-08-30)

- A repeatable static audit of the exact UI/touch configuration and ELF now
  establishes that the frozen 300,172-byte image is **not** a dynamically
  extensible runtime: `CONFIG_FS_ROMFS`, `CONFIG_ELF`, `CONFIG_NXFLAT`,
  `CONFIG_MODULE`, and `CONFIG_PIC` are all disabled. The enabled
  `CONFIG_BINFMT_ELF_RELOCATABLE` option is only a build-output format; it is
  not an on-target ELF loader. The exact stable ELF contains camera/CSI/ISP
  and DSI/GT911 symbols, but has no TFLite Micro, model-container reader,
  workflow state-machine, ELF-loader, NXFLAT-loader, or module-loader symbol.
- Therefore a second application cannot be loaded from Flash/ROMFS into the
  frozen running system. Re-linking a rich image and the four-byte post-link
  extension experiment both previously failed the whole-image safety rule.
  The host-side equal-length-code-space audit is now also closed: the largest
  contiguous `.flash.text` alignment hole is only 102 bytes and lies between
  live functions; it is neither executable extension capacity nor large enough
  for workflow/model logic. Therefore no stable-image binary-patch route can
  honestly deliver the requested buttons or model feature. The next viable
  delivery path is a new, recoverable integrated system image with a strict
  UART/visible-screen/GT911 rollback gate. Evidence and the explicit
  acceptance gate are recorded in
  `docs/m19-frozen-ui-extension-architecture-20260830.md`.

### Minimal Recoverable Workflow-Core Candidate (2026-08-30)

- A new isolated build tree, `/home/max/openvela-p4-min-workflow-core-20260830`,
  was cloned byte-for-byte from the accepted UI/touch source and image before
  modification.  The candidate adds only `workflow_core.c`: a dependency-free
  four-stage release-state selector (`capture`, `gallery`, `label`,
  `inspect`) which has no camera, model, DSI mutation, ROMFS, or Flash path.
  It is invoked only after the existing press/release feedback has been
  restored, and presently reports the selected logical stage through UART.
- `esp32p4_dsi.c` is byte-identical to the frozen baseline source.  A clean
  full build succeeded; it produced 300,288 bytes, SHA-256
  `0a28e173581849f33d3b0b620afbe66449838b6c2184dffc184e5c61d2dd7b65`.
  Its ESP simple-boot header and RAM segment table pass
  `verify_esp32p4_ui_boot_equivalence.py` against the accepted UI image.
  The expected `workflow_core_*` symbols are present; no TFLite Micro,
  model-container, or legacy workflow dependency was added.
- This is an **offline build result only**, not a board acceptance.  Earlier
  evidence establishes that a rebuilt image can black-screen even when its
  boot shape matches the frozen archive.  Accordingly the candidate was not
  written to `COM12`; the model region `0xE0000` was not accessed.  A physical
  retry is prohibited until a stronger DSI relocation-safety hypothesis and
  test distinguish this candidate from the previously rejected same-shape
  images.

### Rebuild Reproducibility and Link-Placement Control (2026-08-30)

- The accepted UI/touch source tree was subjected to `make clean && make -j4`.
  It reproduced the frozen 300,172-byte archive **exactly**, including
  SHA-256 `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`.
  The stable image is therefore reproducible with the present compiler,
  configuration, linker and source tree; a missing object file or changed
  toolchain is not the explanation for later black-screen candidates.
- The active workflow-core candidate moves `camera_diag_main` and all checked
  DSI UI functions by +44 bytes while leaving IRAM/DRAM segment ranges fixed.
  A separate link-only control instead references the new core without using
  it; linker garbage collection removes it and reproduces the stable image
  byte-for-byte. This proves that the current challenge is the placement of
  reachable new code, rather than loss of reproducibility.
- No physical Flash operation was performed for either control. The next
  offline work is to determine whether the ESP32-P4 linker can retain added
  reachable workflow text after all frozen DSI/touch code without changing
  their addresses. Until a deterministic check exists, none of these images
  may be sent to the board.

## Module Register

| ID | Module | Status | Progress | Verified capability / evidence | Next acceptance condition |
| --- | --- | --- | ---: | --- | --- |
| M01 | Board connection and ROM download | Complete | 100% | ESP32-P4 v3.2 identified through `COM12`; Flash read/write verification completed. | Preserve the documented BOOT/RST procedure. |
| M02 | NuttX/OpenVela boot and NSH | Complete | 100% | UI-only image boots and UART returns `nsh>`. | Keep as the regression startup check. |
| M03 | PSRAM initialization and DMA memory | Complete | 100% | 32 MB PSRAM and DMA-accessible external frame buffers verified previously. | Reuse for inference arena after deferred loader acceptance. |
| M04 | Camera control and CSI/ISP capture | Complete | 100% | SC2336 probe and sustained 3/3 RGB565 frames verified. | Reuse only after model loader initialization succeeds. |
| M05 | DSI panel and persistent status UI | Complete | 100% | The board is on the earliest locally archived UI-only press/release image, `artifacts/ui-only-touch-press-release-20260821/nuttx-ui-only-touch-press-release-20260821.bin`, 300,172 B, SHA-256 `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`. It has a visible persistent UI and is recoverable from the independently verified local readback `evidence/m07-ui-only-recovery-readback-20260828.bin`. `0xE0000` was untouched. | Frozen. Richer static layout must be rebuilt as a separate, reversible candidate. |
| M06 | Touch controller and coordinate calibration | Complete | 100% | GT911 `911` at `0x5d`; calibrated coordinates reported on board. | Frozen baseline. |
| M07 | Press/release visual feedback | Complete | 100% | GT911 input, X/Y calibration, press/release callbacks and framebuffer drawing are verified on the accepted UI-only archive (SHA-256 `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`). On 2026-08-28 the user confirmed feedback remains active long-term: the touched area darkens while pressed and restores after release. The external model container was not written. | Frozen. Any later UI change must retain this exact visible press/release behaviour. |
| M08 | UI result overlay | Complete | 100% | Status UI is rendered on DSI and supports local framebuffer updates. | Bind real inference result states after M13 and M14. |
| M09 | YOLOv8 model export and INT8 conversion | In progress | 98% | The final 320px full-INT8 artifact is identified: `artifacts/model-int8-20260821/best_saved_model/best_full_integer_quant.tflite`, SHA-256 `3d08c041...b2c042`; contract `1x320x320x3 int8` to `1x14x2100 int8`. Its OVM10 container SHA-256 is `602afa11...019fc2`. | Obtain a board-side successful `Invoke()` record before freezing it as the deployment model. |
| M10 | Model storage outside boot image | Complete | 100% | Re-accepted on 2026-08-21: restored M10 baseline application SHA-256 `30136567...30b4cc87` to `0x2000` with esptool hash verification; the model container remained at `0xE0000`. The physical board booted NuttX/DSI/GT911 and `camera_diag --model-storage` returned `validation=PASS`: 3,234,866 B, SHA-256 `10fab841...b7ffad85`, pointer `0x40041000`, MMU free pages `1020` before and after release. The migration rollback capture `evidence/m13-model-container-readback-20260822.bin` has now been independently parsed: OVM10 v1, 3,244,032 B total, model length 3,234,866 B, and manifest/payload SHA-256 both `10fab841...b7ffad85`. | Preserve this verified capture; do not modify `0xE0000` until the accelerated-layout audit and rollback procedure are accepted. |
| M11 | TFLite Micro runtime initialization | Complete | 100% | Physical-board evidence from 2026-08-22 records real LOGISTIC preparation using `MicroContext` quantization and `QuantizeMultiplier()` under `TFLITE_EMULATE_FLOAT`, with `camera_diag --tflm-init` completing `AllocateTensors status=0 elapsed_ms=20 arena_used=870036`, input `1x256x256x3`, output `1x14x1344`, and returning `nsh>`. The previously recorded 435,012-byte artifact SHA (`9bb42d217...e1c`) does not match the image now recovered from the immutable low-Flash audit (`ff8e3a65...e1c`); the latter also fails before NSH in the current environment. The functional 2026-08-22 result remains historical evidence, but its exact accepted binary provenance requires reconciliation. | Frozen runtime logic; do not use either legacy M11 image as a current boot control. Reuse only after the stable-extension architecture is accepted. |
| M12 | Camera preprocessing for model input | Complete | 100% | `--isp-frame` now pauses the persistent DSI UI, captures real ISP RGB565 frames, calls `camera_diag_tflm_preprocess_diag` on a real frame, and restores the UI on success or failure. Physical-board evidence: `ISP_SUSTAINED 3/3`, `tflm_preprocess: rc=0 checksum=0xbde6ffe4 min=-103 max=-71 bytes=196608`, followed by `status UI restored rc=0` and `nsh>`. | Freeze the accepted converter contract for M13 inference input. |
| M13 | YOLO output decoding and defect decision | In progress | 96% | External Flash was read back without modification and its manifest/model SHA-256 was revalidated as `10fab841...b7ffad85`, 3,234,866 B. The deployed contract is input `1x256x256x3`, output int8 `1x14x1344`, scale `0.005286871`, zero-point `-128`. Semantic policy maps only `stain`, `damage`, and `wrinkle` (IDs 5--7) to defects; energy levels, `label`, and `box` are structural. The board passed semantic NMS self-test (`evidence/m13-semantic-nms-selftest-20260823.log`). A real 3/3 ISP frame run completed preprocessing (`checksum=0x906a5175`) and `Invoke()` in 111700 ms; top class was structural `label` (ID 8), `defect=0`, `nms_kept=0`, `defects=0`. The target offline-validation image, containing RGB565 stain/damage/wrinkle samples and using the same board preprocessing, TFLM invoke, decode and semantic policy, was built as 839,476 B, SHA-256 `093349b4...57363780`, then written only at `0x2000`; write-time hash and standalone `verify-flash` passed. On the physical target stain decoded as class 5 with `defects=1` in 111910 ms; the damage sample decoded both class 6 `damage` and class 5 `stain` (`defects=2`) in 111940 ms; the wrinkle sample decoded only its real class 5 `stain` (`defects=1`) in 111950 ms and missed its two class 7 `wrinkle` annotations. Structural candidates did not create defects. Evidence: `evidence/m13-esp-stain-20260823-serial.log`, `evidence/m13-esp-damage-20260823-serial.log`, and `evidence/m13-esp-wrinkle-20260823-serial.log`. | Add startup/inference mutual exclusion, then audit target-side class-7 confidence/threshold behavior and output parity before acceptance. |
| M14 | Inference-to-display result binding | Complete | 100% | Accepted on physical board, 2026-08-22: after real 3/3 capture and `Invoke()`, the UI resumed then updated its result card with `dsi: inference result candidates=0` and `camera_diag: inference result UI rc=0 candidates=0`. A controlled self-test also returned `dsi: inference result candidates=2`, `rc=0`, and `nsh>`. Ten later GT911 events, including result-card coordinates, each recorded `dsi: touch press` followed by `dsi: touch release restored UI`; the result update redraws and cache-syncs only its card and does not regress the frozen feedback. Evidence: `evidence/m14-result-ui-20260822-serial.log`, `evidence/m14-result-ui-selftest-20260822-serial.log`, `evidence/m14-touch-result-card-20260822-serial.log`. No-candidate remains explicitly non-business-PASS pending M16. | Frozen. Bind verified class/defect semantics when M13/M16 are accepted. |
| M15 | End-to-end camera-to-inference performance | In progress | 80% | The OpenVela/NuttX tree builds a read-only `camera_diag --espdl-storage` validator. Physical Flash audit confirmed 16 MB and an erased, 64 KB-aligned region at `0xB90000`. The verified 3,288,288 B ESP-DL v2 container was written only there; the application was written only at `0x2000`, and both esptool operations reported `Hash of data verified`. After changing the runtime image declaration from 4 MB to 16 MB, the physical board returned `validation=PASS bytes=3284192 sha256=1d17062e...b7bc9b offset=0xb90000`; DSI, GT911, and NSH stayed normal. The inner payload contract is also validated as `EDL2`, mode 0, 3,284,176 B FlatBuffer plus zero fill; see `docs/m15-espdl-inner-contract-20260823.md` and `tools/espdl_model_contract.py`. The direct port seam is now identified: construct `fbs::FbsModel` from the EDL2 FlatBuffer span, then `dl::Model(fbs_model, ...)`; see `docs/m15-espdl-nuttx-port-seam-20260823.md`. The actual graph operation set is recorded in `docs/m15-espdl-graph-ops-20260823.md`; all available ESP-DL `dl_base_*.cpp` implementations are compiled, and all 38 required P4 ISA source files now assemble with explicit `rv32imafdcv_xespv_xesploop` flags. The stable 16 MB NuttX image links and passes `esptool.py elf2image`; the refreshed binary is 439,184 B, SHA-256 `4a4a538a451bbdc87b27d55162df1133a918fc81ba0019e074e06d912997d898`. Target-side TFLM validation runs measured 111910--111950 ms per inference including this graph's actual model execution. Evidence: `evidence/m15-espdl-storage-pass-20260823-serial.log`, `evidence/m15-espdl-all-base-build-20260823.log`, `evidence/m15-espdl-isa-stable-build-20260823.log`, `evidence/m15-espdl-isa-complete-build-20260823.log`, and the M13 ESP validation logs. Runtime entry exposure still reveals missing NuttX C++ ABI/container symbols and additional FbsModel method linkage; no ESP-DL runtime image has been written to the board. | Add the minimal compatible C++ ABI/FbsModel linkage seam, expose the probe safely, then run the model against a captured camera frame and record latency/output parity before any production-board write. |
| M16 | Defect-detection accuracy validation | In progress | 73% | The current `models/best.pt` was batch-evaluated again on all 285 validation images with the frozen deployment parameters: input 320, confidence 0.65, IoU 0.45. Result: precision 0.9483, recall 0.8431, mAP50 0.8995, mAP50-95 0.6850. Defect AP50: stain 0.9950, damage 0.6329, wrinkle 0.8451; damage remains the principal weakness. Machine output: `E:\energy_label_defect_detection\P\outputs\m16_validation_20260823\host_bestpt_320_conf065_iou045\summary.json`. Three known validation images were run fully on the ESP target: stain annotation (class 5) was hit; the damage image's class 6 and class 5 annotations were both hit; the wrinkle image's class 5 annotation was hit but both class 7 annotations were missed. The three target logs are `evidence/m13-esp-stain-20260823-serial.log`, `evidence/m13-esp-damage-20260823-serial.log`, and `evidence/m13-esp-wrinkle-20260823-serial.log`. The 2026-08-29 read-only split audit found 5,432 train pairs and 285 val pairs, with no missing/orphan labels, but one exact image-content leakage group: a val image duplicates four train files. `test` and `val` both point to `images/val`, so it is not an independent final-test result. Evidence: `evidence/m16-dataset-split-audit-20260829.json`, repeatable tool `tools/m16_dataset_split_audit.py`, and `evidence/m16-validation-manifest-20260829.md`. | Obtain a labeled, untouched independent test set (existing files cannot be relabeled as independent), run the second target batch, audit class-7 target confidence/threshold behavior and quantized-to-float output parity, then agree and verify class-specific acceptance thresholds. |
| M17 | Product reliability and final system acceptance | In progress | 88% | Startup, camera, touch, external model storage, real TFLite Micro allocation, real-frame inference, bounded NMS, and both zero/nonzero result-to-display states have physical-board evidence. The live external-model container has a verified read-only rollback capture. The board currently has user-confirmed visible UI and touch feedback on the recovered UI-only image, but richer application restoration and automatic touch startup persistence remain to be revalidated. | Freeze the recovered UI-plus-touch state, then restore features and pass touch/no-flicker, performance, and accuracy validation. |
| M18 | Energy-level result UI | Complete | 100% | 2026-08-24: cross-build, physical-board deployment, user visual confirmation, and real target inference passed. The highest-confidence accepted energy class (IDs 0--4) is rendered as `ENERGY LEVEL: 1` through `5` from the same NMS result, with no additional invocation. The self-test returned level 3; a real stain sample completed in 111890 ms and updated DSI with `candidates=3 energy_level=5`. Application was written only to `0x2000`, 839,748 B, SHA-256 `3fa107de...dca20ac`; evidence `evidence/m18-energy-level-ui-build-20260824.log`. | Frozen. Keep the single-inference result contract. |
| M19 | Inspection workflow UI | In progress | 70% | Two-row controls, label gate, button hit-testing, stage state, and Y-axis mapping are implemented in the historical rich branch. The accepted archive-preserving text update is physically visible and retains touch feedback, but it changes only existing text and cannot add button geometry or hit-testing. The rich 841 KB historical images are rejected display candidates; a rebuilt minimal control also blue-screened, proving the prior rebuild output was unsafe for board UI changes. On 2026-08-29, the callback behaviour was separated into an offline `workflow_state` state machine: touch dispatch is constant-time; a worker owns capture/inference; busy, no-image, LABEL-gate, stale-completion and successful-completion paths are host-tested. The gate now also rejects LABEL until CAPTURE has completed. Evidence: `.codex/workflow-binding-20260829/workflow_state_test.c`; its VM run returned `workflow state verification: PASS`, and `tools/verify_workflow_hit_regions.py` returned `PASS (8 buttons, gutters inert)`. The stable build was then recovered in the fully self-contained VM worktree `/home/max/openvela-p4-stable-recovery-isolated-20260829`: the exact archive-era config, app sources, DSI source, Makefile, board startup argument `--touch-ui-live`, compiler-prefix shim and archive timestamp were restored. Its freshly linked `nuttx.bin` is 300,172 B with SHA-256 `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`, byte-identical to the accepted archive (`stable_ui_provenance=PASS`). A static-button build was then compiled offline only: `artifacts/static-buttons-20260829/nuttx-static-buttons-20260829.bin`, SHA-256 `46584672...ef402b46`; it grew by 432 B and changed 135,509 overlapping bytes, so it is rejected and has not been flashed. The exact baseline ELF/map/bin were archived at `artifacts/exact-stable-20260829` on the VM. Its apparent 19 KB VMA alignment gap is absent from the final mapped image and is not a code cave. The 368-byte in-place mode-4 status-drawing block was also audited: it has exactly five fills and eight text draws for the accepted title/two-card UI; it cannot make eight separate controls without extra instructions or loss of required content. `tools/verify_ui_binary_patch.py` fail-closes any candidate whose changed bytes leave explicit approved spans or touch protected press/release code. The first complete workflow candidate was built offline as `dd3c28ec...9ebf336` but rejected because every redraw uses DMA stop/start; the stable source was restored and clean rebuild reproduced the frozen SHA exactly. The 2026-08-29 post-link extension experiment added one isolated 4-byte `workflow_extension_anchor` after all original flash text. DSI and touch symbol addresses stayed fixed, but the final ESP image grew to 300,176 bytes and changed byte range `0x1dffc:0x1dffd` in the packed image; this fails the startup-equivalence rule. The extension was never flashed, and the isolated worktree was rebuilt back to the exact frozen SHA. | Build a separate recoverable workflow image around the already host-tested state machine, with cache-sync-only display updates, a hardware-observable display/touch acceptance loop, and immediate rollback to the frozen 300172-byte image. |

### M15 latest delta (2026-08-23)

### M13/M16 defect-first decoder audit (2026-08-29)

- Added `tools/m16_defect_first_decoder_trial.py`.  It uses the deployed
  `1x256x256x3` INT8 model, the same RGB565 restoration and input quantization
  as the target, but prevents structural classes from hiding defect scores at
  the same anchor.
- The complete 285-image validation sweep is saved in
  `evidence/m16-defect-first-decoder-trial-20260829.json`.  The usable
  F1-ranked candidate is raw threshold `-56`, candidate cap `16`, IoU `0.45`:
  stain 285/285 (precision 100%), damage 36/38 (recall 94.74%, precision
  97.30%), wrinkle 33/36 (recall 91.67%, precision 97.06%).
- This is an offline result only; it proves the deployed output tensor retains
  defect evidence discarded by the current top-class-per-anchor policy.  No
  firmware or Flash region was changed.  A future board candidate must add
  this bounded defect-first decoder, pass the frozen UI boot gate, and be
  tested against both known samples and an independent labeled set.

### M13/M16 per-class threshold audit (2026-08-30)

- Added `tools/m16_defect_first_per_class_search.py`. It caches the exact
  ESP-equivalent INT8 outputs and runs a reproducible two-stage threshold
  search, avoiding repeated inference and an impractical full Cartesian NMS
  loop.
- Best bounded candidate: defect-first decoding, candidate cap `16`, same-class
  NMS IoU `0.45`, raw thresholds `stain=-120`, `damage=-88`, `wrinkle=-56`.
  On 285 validation images it gives stain 285/285 with 0 false positives,
  damage 38/38 with 1 false positive, and wrinkle 33/36 with 1 false positive.
  Evidence: `evidence/m16-defect-first-per-class-search-20260830.json`.
- This resolves decoder-side damage misses in the ESP-equivalent path. The
  remaining three wrinkle misses require model/data improvement or explicit
  acceptance of that limit; no firmware or target Flash was changed.

### M16 residual-wrinkle root-cause audit (2026-08-30)

- Added `tools/m16_wrinkle_miss_audit.py` and
  `tools/m16_wrinkle_miss_float_audit.py`. The residual samples are
  `2026-03-05_16-32-46_319`, `2026-03-05_17-39-12_837`, and
  `A02_L4_T01_DAM_035`; evidence is
  `evidence/m16-wrinkle-miss-audit-20260830.json` and
  `evidence/m16-wrinkle-miss-float-audit-20260830.json`.
- `2026-03-05_16-32-46_319` has a strong original 320px float wrinkle result
  (0.722) aligned with truth, but the 256px RGB565 INT8 path produces only
  weak/misaligned wrinkle candidates: deployment resolution/quantization is
  the likely cause. `2026-03-05_17-39-12_837` has no original float wrinkle
  candidate even at confidence 0.01: it is a source-model/data limitation.
  `A02_L4_T01_DAM_035` has a correctly placed but weak original float result
  (0.097), also below normal operating confidence.
- Therefore decoder threshold changes cannot recover all three responsibly;
  the highest-value follow-up is a 320px INT8 deployment feasibility test or
  targeted retraining/augmentation for these wrinkle patterns. No target
  firmware or Flash was changed.

### M09/M16 320px INT8 feasibility and accuracy audit (2026-08-30)

- `tools/m16_320px_tflm_feasibility.py` confirms the archived 320px full-INT8
  artifact is 3,235,296 bytes (SHA-256 `3d08c041...11b2c042`), only 430 bytes
  larger than the active 256px artifact. Its conservative arena projection is
  1,220,088 bytes, based on the physical 256px `870,036` byte measurement;
  that is 3.64% of the available 32MB PSRAM. It fits the existing verified
  model-container capacity, but must pass a real isolated `AllocateTensors`
  measurement before any deployment claim.
- `tools/m16_320px_defect_first_trial.py` evaluated all 285 validation images
  through 320px RGB565 restoration and full-INT8 inference. Best bounded
  parameters are candidate cap 16, IoU 0.45, raw thresholds
  `stain=-128`, `damage=-72`, `wrinkle=-8`: stain 285/285, damage 38/38 and
  wrinkle 34/36, with no image-level false positives in this validation set.
  Evidence: `evidence/m16-320px-tflm-feasibility-20260830.json` and
  `evidence/m16-320px-defect-first-trial-20260830.json`.
- The 320px candidate improves wrinkle recall by one image over 256px, but it
  cannot recover the known source-model miss. No board image or model Flash
  region was modified.

### M09/M11/M16 isolated 320px allocation preparation (2026-08-30)

- Generated and independently parsed the candidate OVM10 model container:
  `artifacts/model-int8-20260821/best_saved_model/best_full_integer_quant.ovm10`.
  It contains the 3,235,296-byte 320px INT8 FlatBuffer with SHA-256
  `3d08c041c9777d8238df21fe7b7d600664c6c8049212277a28d8e05711b2c042` and
  has a total length of 3,239,392 bytes. This is 4,640 bytes smaller than the
  verified 3,244,032-byte readback backup at
  `evidence/m13-model-container-readback-20260822.bin`; both manifests and
  payload hashes were checked locally.
- Prepared an isolated VM-only NuttX candidate whose ROMFS startup runs only
  `camera_diag --tflm-init`. It deliberately does not start DSI, GT911, camera,
  or `Invoke()`, so it is suitable only for a reversible real-hardware
  `AllocateTensors` measurement. Its full cross-build completed successfully:
  `nuttx.bin` is 841,028 bytes, SHA-256
  `c25b6e673578707cf1973e56a858a8d2fc41653c00ecf44ce8993a481a9e8e59`, and
  the generated ROMFS contains only `camera_diag --tflm-init`. The stable board
  image and target Flash remain untouched. No write to `0x2000` or `0xE0000`
  was performed by this preparation step.
- A one-time physical attempt was then made with the isolated candidate and the
  320px OVM10 container. Both candidate writes passed esptool write-time hash
  verification. UART reached `*** Booting NuttX ***` and loaded all seven image
  segments but did not reach the NuttX shell or `camera_diag --tflm-init`, so
  no 320px `AllocateTensors` measurement was obtained. The experiment was
  immediately rolled back: an independent readback confirms the stable UI
  application is byte-identical to SHA-256
  `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`, and
  the original model container is byte-identical to SHA-256
  `38f2915a83b74fcb8dae4dbaea422cae9a47b930ef8167697869128c08d26ad3`.
  Future 320px tests must first identify the early-startup delta of the 841KB
  diagnostic image; do not repeat this candidate unchanged.
- Post-rollback UART acceptance passed: `*** Booting NuttX ***`, `NuttShell
  (NSH)`, `dsi: persistent status UI active`, and `GT911 product=911` were all
  observed. The board is therefore back on the accepted visible UI and
  long-running touch-feedback baseline.

The controlled ESP-DL compatibility pass enumerated the P4 SoC, ESP-IDF ROM/log/timer/MMU, flash, partition, and scheduler header surfaces and moved the probe past the earlier missing-header chain. The VM's installed ESP-IDF RISC-V toolchain was exposed through a temporary name shim; the OpenVela tree now compiles the Tensor and Tool implementations (`dl_tensor_base.cpp`, `dl_tool.cpp`, `dl_tool_cache.cpp`) and a major quantized operator subset (Conv2D, DepthwiseConv2D, elementwise arithmetic, activation, resize, pooling, padding, normalization, LUT) alongside the model-layer sources with the real NuttX C++ flags. The probe explicitly calls `dl::Model::run()`, and the complete 16 MB image links and passes `esptool.py elf2image` generation without a board write. The only compatibility adjustment in this pass is using the portable round-half-even fallback instead of the ESP-DL inline `fcvt.w.s` constraint that this NuttX toolchain rejects. Remaining graph operators, P4 ISA kernels, and physical runtime execution still require validation. See `docs/m15-espdl-header-probe-20260823.md`, `evidence/m15-espdl-model-sources-build-20260823.log`, `evidence/m15-espdl-run-link-20260823.log`, and `evidence/m15-espdl-base-subset-build-20260823.log`.

The full available `dl/base/dl_base_*.cpp` set was then compiled and the static 16MB image still linked successfully (`evidence/m15-espdl-all-base-build-20260823.log`). A temporary `--espdl-probe` command experiment demonstrated the remaining runtime gate: directly retaining every base implementation pulls P4 ISA assembly symbols and unsupported NuttX C++ library symbols (`std::__*`); the command was removed and the last known-good image rebuilt. No frozen UI/touch path was changed. The next implementation pass must provide the selected P4 ISA objects and a compatible C++ runtime seam before exposing the probe on the physical board.

## Mandatory Operating Protocol

1. Before beginning a new technical step, read this ledger and identify the
   target module and its dependencies.
2. Immediately after a build, code change, hardware action, board observation,
   benchmark, or validation result, update the affected row with its new
   percentage, status, evidence path, and next acceptance condition.
3. When a result is unknown, conflicting, or the working context needs to be
   reconstructed, stop relying on chat history and review this ledger together
   with the evidence paths named in the affected row before taking action.
4. Every user-facing progress report must be derived from the current Module
   Register, including all complete, in-progress, pending, and blocked modules.
5. A dependent module failure does not lower a completed module's percentage.
   Record the blocker in the dependent module unless a direct regression is
   observed and evidenced.
6. Before declaring project completion, every row must be either `Complete` or
   explicitly accepted by the user as out of scope; no `Pending`, `In progress`,
   or `Blocked` row may be omitted from the final report.

## Current Work Order

### Blue-screen recovery record (2026-08-29)

- The full rebuilt workflow-button candidate (`2364b559...`) remains rejected: it produced a blue screen and must not be flashed again.
- A read-only `verify-flash` check first proved that the board had the text-safe image at `0x2000` (`cf4b03b0...bb6cf14`). The model-container offset `0xE0000` was not read or written during recovery.
- Because the display was still reported blue with that image after reset, the verified archival UI/touch baseline was restored to `0x2000` only: `artifacts/ui-only-touch-press-release-20260821/nuttx-ui-only-touch-press-release-20260821.bin`, SHA-256 `22e48e28...e743`.
- Both esptool write-time hash verification and a separate `verify-flash` passed. On 2026-08-29 the user confirmed that the physical display recovered normally. The archival UI/touch image is again the active recovery baseline.
- The USB serial endpoint did not emit a usable startup log after either reset, so physical visual confirmation was used as the recovery signal. No further UI build will be flashed until it can be shown not to perturb this frozen baseline.

### Stable-UI rebuild audit (2026-08-29)

- The rejected workflow-button image (`2364b559...`) has the same entry point and initial load addresses as the stable archive, but it differs from the archive from file offset `0x1F5` onward in 6,267 discontiguous intervals across 68 of 74 4 KiB pages. Its final load segment also grows by 16 bytes and its total image by 1,100 bytes.
- Therefore it is a whole-image relink, not a safe button-only patch; see `docs/handoffs/23-stable-ui-rebuild-audit-20260829.md`. It must never be re-flashed.

### Stable-UI provenance recovery (2026-08-29)

- The original stable binary is preserved on `openvela-vm-jul` as `/home/max/openvela-p4-dwgdma-api-probe-20260828/nuttx/nuttx-ui-only-touch-press-release-20260821.bin`; it hashes exactly to the active recovery baseline `22e48e28...e743`.
- Two same-day configuration snapshots have identical SHA-256 `fdef6a91...15a3e`: `.config.pre-ui-only-20260821-1700` and `.config.pre-ui-only-touch-build-20260821-1703`. The NuttX and Apps source repositories and the original build log also remain on the VM.
- The final UI DSI source was an uncommitted workspace change and is not yet exactly recovered. A fail-closed image gate was added at `tools/verify_stable_ui_provenance.py`: the stable archive passes and the rejected button image fails, as recorded in `docs/handoffs/24-stable-ui-provenance-recovery-20260829.md`. No reconstruction image may be flashed before this gate passes.

1. M15: restore the `openvela-vm-jul` SSH/download-path connection, run only
   read-only `flash-id` and layout reads, then design a reversible accelerated
   model-storage migration before any model write.
2. M16 and M17: freeze an independent validation split and complete the
   accuracy and final acceptance protocol.

## Evidence Added This Update

- The complete project handoff is `docs/handoffs/18-esp32p4-openvela-defect-detection-complete-handoff-20260826.md`. It summarizes the module register, current board image, operating commands, DSI refresh blocker, UI workflow, safety rules, and final acceptance conditions.

- `E:\openvela\evidence\m12-csi-dsi-lifecycle-20260822-serial.log` records
  the physical-board regression after writing only the application image to
  `0x2000`: the persistent DSI UI is paused, three 1228800-byte ISP RGB565
  frames complete, real-frame TFLM preprocessing returns `rc=0` with checksum
  `0xbde6ffe4`, min/max `-103/-71`, and 196608 output bytes, then the DSI UI
  is restored with `rc=0` and NSH returns.
- The accepted M12 application image is
  `openvela-vm-jul:/home/max/openvela-p4-integration/artifacts/m12-csi-dsi-lifecycle-20260822/nuttx-m12-csi-dsi-preprocess-20260822.bin`,
  442072 bytes, SHA-256
  `babba562b2662d3db8c6f7567329de9e2c44a1f1f4a3cc67619582d51375be25`;
  esptool reported `Hash of data verified` at `0x2000`.
- `E:\openvela\evidence\m13-model-container-readback-20260822.bin` is a
  read-only 0xE0000 container capture. Its verified manifest contains the
  live `10fab841...b7ffad85` model, 3,234,866 bytes, `1x256x256x3` int8 input,
  and `1x14x1344` int8 output. This supersedes the stale 320x320/2100 output
  assumption for the current target model.
- Physical board regression on 2026-08-22, latest application written only at
  `0x2000`: `camera_diag --isp-frame` logged `ISP_SUSTAINED 3/3`,
  `tflm_invoke: status=0 elapsed_ms=112010`, and
  `tflm_decode: top_anchor=1315 class=8 score_raw=-111 threshold_raw=-5
  accepted=0`, followed by `status UI restored rc=0` and `nsh>`.
- The NMS build `artifacts/m13-nms-selftest-20260822/nuttx-m13-nms-selftest-20260822.bin`
  (SHA-256 `aa7fbeb5b1f4311d1d59384110116e310c9192c85a26d3e7ac7b0c4e9642b251`)
  was written only at `0x2000` and verified by esptool. On the physical board,
  `camera_diag --tflm-nms-selftest` returned `same_class_overlap=suppressed`,
  `cross_class_overlap=kept`, `low_score_rejected=1`, `kept=2`, and `PASS`.
  The automatic persistent DSI UI, GT911 detection, and `nsh>` also remained
  normal. Full record: `evidence/m13-nms-selftest-20260822-serial.log`.

- `E:\openvela\.embeddedskills\logs\serial\ui-only-touch-press-release-verify-20260821-1720.log`
  records GT911 presses, coordinate updates, `dsi: touch press`, and
  `dsi: touch release restored UI` for the accepted press/release interaction.
- The current image was written only to `0x2000` and esptool reported
  `Hash of data verified`.
- Model-storage design evidence: `best_integer_quant.tflite` is 3,235,560 B
  with SHA-256 `5db1a2c9d2b56694b0248172ddcd300a3a61ca451f0139299c26f6f67ba1bae1`.
  The ESP32-P4 SDK provides `spi_flash_mmap()`, `spi_flash_munmap()`, and
  `spi_flash_mmap_get_free_pages()`; the mapped Flash source must be 64 KB
  aligned.
- `E:\openvela\tools\model_container.py` generated
  `artifacts/m10-model-storage-20260821/best_full_integer_quant.container.bin`
  (3,238,962 B) from the 3,234,866 B source-tree model. The M10 UI-only
  storage-diagnostic build is
  `artifacts/m10-model-storage-20260821/nuttx-m10-model-storage-20260821.bin`
  (302,356 B, SHA-256
  `292ba7a6825fcf542bd06b8ecf9185fab19fa61acd37e386149a257db4afd22e`).
  Its remote cross-build completed successfully on `openvela-vm-jul`.
- 2026-08-21 Windows serial scans do not list `COM12`. This is not a board
  validation prerequisite because the intended connection is USB passthrough
  into `openvela-vm-jul`, not a Windows-host serial workflow.
- USB diagnosis evidence: Windows Plug and Play logs record repeated `vmusb`
  event 3 failures, `Unable to create device object`, at the times of recent
  replug attempts. Device Manager lists two error-state `VMware USB Device`
  entries and no ESP32/CP210/CH34/FTDI serial device. `VMUSBArbService` is
  running, but `Restart-Service -Name VMUSBArbService -Force` was denied
  because the current process is not elevated. After the user performed the
  elevated restart, the serial scan still listed only the six Bluetooth ports;
  no ESP/USB-UART device or `COM12` appeared.
- Corrected connection-path evidence: `openvela-vm-jul` is the target USB
  owner. Its `lsusb` currently lists only VMware virtual USB hubs and no
  physical board; `/dev/ttyACM*` and `/dev/ttyUSB*` are absent. The board must
  be explicitly connected to the VMware guest before model-storage flashing.
- VMware log evidence identifies the actual board as `USB JTAG/serial debug
  unit` (`VID:PID 303A:1001`). VMware attempts its connection but logs
  `USBArbLib: Received message size ... exceeds maximum size (4096)` followed
  by `Failed to connect device, failedStatus(11)`; this is an arbitrator/client
  protocol failure, not a board or cable failure.
- USB recovery and M10 write evidence: after cleanly restarting the target VM,
  `303A:1001` is present as `/dev/ttyACM0`. esptool wrote the 302,356 B M10
  application to `0x2000` and the 3,238,964 B model container to `0xE0000`;
  both writes reported `Hash of data verified`. `CONFIG_UART0_SERIAL_CONSOLE=y`
  confirms that NuttX output requires the separate CH340 UART, not the USB
  JTAG download port.
- M10 board acceptance evidence, 2026-08-21: switched the diagnostic image to
  the official `CONFIG_ESPRESSIF_USBSERIAL=y` console path and wrote only the
  302,612 B app at `0x2000` (SHA-256
  `301365677b171acdd454c3a9c76a74dc1b2e002707c82e9a3e54f46530b4cc87`).
  The external container at `0xE0000` was retained. On the physical board,
  `camera_diag --model-storage` printed `validation=PASS`, the expected model
  length and SHA-256, and `mmu_free_before=1020` / `mmu_free_after=1020`.
  USB console output also records NuttX NSH, persistent DSI UI start, and GT911
  detection. The accepted app is stored as
  `artifacts/m10-model-storage-20260821/nuttx-m10-model-storage-usbconsole-20260821.bin`.

## Update Rules

## Latest M15 ABI Audit (2026-08-23)

- The ESP-DL 3.3.9 component metadata identifies source commit
  `12c0616de145b704e1149c474b9a1e852e631d67`. Its CMake build explicitly
  links `fbs_loader/lib/${IDF_TARGET}/libfbs_model.a`; the component cache
  contains `fbs_model.hpp` but no recompileable `fbs_model.cpp`.
- The ESP32-P4 archive declares RISC-V single-float (`..._f2p2_...`) and GNU
  C++11 container ABI usage. It cannot be mixed directly into the current
  soft-float NuttX top-level link.
- An official-source audit checkout was attempted on `openvela-vm-jul` at the
  declared commit. It is presently blocked by the VM DNS failure
  `Could not resolve host: github.com`; no source or target image was changed.
- The same exact commit was independently inspected through the official
  GitHub tree API from the local workstation. It confirms that
  `fbs_loader/src` contains only `fbs_loader.cpp`; there is no public
  `fbs_model.cpp` to rebuild for NuttX.
- Build evidence for the completed 38-source ISA closure is
  `evidence/m15-espdl-isa-complete-build-20260823.log`. No M15 runtime image
  was flashed.

## Latest M13/M16 Semantic Audit (2026-08-23)

- `E:\energy_label_defect_detection\P\config.yaml` declares class IDs 5, 6,
  and 7 (`stain`, `damage`, and `wrinkle`) as defects. IDs 0--4 are energy
  levels; ID 8 (`label`) and ID 9 (`box`) are structural/localization classes
  and must not independently produce a defect decision.
- The supplied `dataset.yaml` contains 5,432 train image/label pairs and 285
  validation image/label pairs. Its `test` and `val` entries both select
  `images/val`, so it is a validation split, not a second independent test
  split. M16 remains unaccepted until a separate untouched test split and
  target-output parity evidence exist.

## M05 Automatic UI Recovery Build (2026-08-23)

## M05/M07 Stable-Baseline Preservation (2026-08-28)

- The application currently on the board was read back through the VM and
  saved locally as `evidence/m07-ui-only-recovery-readback-20260828.bin`.
  Its size is 300,172 bytes and SHA-256 is
  `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`.
- The readback hash exactly matches the accepted UI-only press/release archive.
  This confirms a recoverable visible-UI and touch-feedback baseline before
  any new automatic-startup or rich-UI candidate is built. No flash write was
  performed and the external model container at `0xE0000` remains untouched.
- Next: establish exact source provenance for this archived image, then build
  a minimal NSH-script automatic-start candidate that changes only startup
  behaviour. It must pass build and flash verification before any board test.

## Stable Image Whole-File Reproduction Audit (2026-08-29)

- A genuinely independent VM copy was created at
  `/home/max/openvela-p4-wholeimage-repro-20260829`: its copy operation
  dereferenced the accidental cross-worktree links, and its normal local
  `apps/platform/board -> dummy` build link was then restored. This removes
  the prior false isolation where board sources and old application objects
  could still be imported from the live integration tree.
- The clean single-threaded rebuild completed at exactly 300,172 bytes, the
  same size as the accepted archive, but its SHA-256 was
  `2a4dbf285ad20140c915fed752513c4cd2c41a063ea7b5588be696566dd44cec`,
  not the accepted `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`.
  It differs in 24,086 byte positions and was **not flashed**.
- Flash-string comparison exposed a concrete historical-startup mismatch:
  the accepted archive contains `--touch-ui-live`, whereas the first isolated
  reconstruction used a later `--dsi-ui-live` boot source. Rebuilding only
  with the preserved historical `--touch-ui-live` startup source completed
  at 300,172 bytes with SHA-256
  `3e0e5093a74ecedfa996ab24fc7032697a1f8aa487386d2ea6bdfa876e3e935e`.
  It is closer in provenance but still not byte-identical, so it is also
  rejected for board testing.
- Safety decision: no reconstructed image may be written at `0x2000` until
  its full-file SHA-256 equals the frozen archive. The accepted board image
  and the external model container at `0xE0000` remain untouched.

## Corrected Isolated-Link Reproduction (2026-08-29)

- A second isolated copy was created at
  `/home/max/openvela-p4-wholeimage-repro2-20260829` by preserving NuttX's
  normal local symlink structure and rewriting only absolute links that
  previously escaped to `/home/max/openvela-p4-integration`. The copy has 31
  symlinks, with zero links back to the live integration tree.
- The copy was rebuilt with the recovered 8/21 camera/DSI sources, historical
  touch-startup source, minimal UI Makefile, and archived UI configuration.
  The resulting image is exactly 300,172 bytes, SHA-256
  `691e091d991b928d500ce2ed5935edf36c75e10562e867bbd5c2cd50175ca1c3`,
  still different from the accepted archive. It was not flashed.
- This establishes that the remaining mismatch is in historical build inputs
  or generated/link metadata, not merely accidental cross-worktree symlinks.
  The frozen board image at `0x2000` and model container at `0xE0000` remain
  untouched.

## Historical Build-Environment Audit (2026-08-29)

- The accepted archive's source files and configuration are available, but the
  complete historical generated-build state is not: the live integration tree
  now contains a later 841,028-byte model-enabled `nuttx.bin`, while the
  accepted UI archive is 300,172 bytes and has no corresponding historical
  ELF/map/object bundle.
- Two independent reconstructions using the recovered sources, configuration,
  and locally rewritten symlink trees both produced 300,172-byte images with
  different SHA-256 values (`3e0e5093...` and `691e091d...`). This proves the
  missing historical build inputs/metadata cannot be safely inferred from
  source files alone.
- Decision: stop attempting blind whole-image reproduction. The board remains
  on the frozen verified image; the next implementation path must branch from
  that exact artifact with a separately reversible change and an explicit
  board rollback image, rather than treating a same-size rebuild as safe.

## UI Text-Only Candidate Audit (2026-08-29)

- The retained candidate `nuttx-ui-textsafe-20260828.bin` was recovered from
  the VM and copied locally to `.codex/nuttx-ui-textsafe-20260828.bin`.
  It is 300,172 bytes, SHA-256 `cf4b03b0d61d04617b86cff29e910060277025b6e0e4395f78cf53c32bb6cf14`.
- A local byte-level comparison against the accepted archive found exactly
  105 differing bytes, all in contiguous ASCII UI text storage
  `0x1091D..0x1099B`. Startup code, task parameters, DSI DMA, GT911 touch
  handling, press/release drawing, all other application bytes, and the model
  container are unchanged.
- The candidate is a narrowly-scoped visual-text upgrade only, not a button
  binding implementation. Full audit and rollback rules are recorded in
  `docs/handoffs/21-ui-textsafe-candidate-audit-20260829.md`. It has not been
  flashed in this stage and does not replace the frozen baseline until a
  physical visible-UI and long-term touch-feedback acceptance is recorded.

## UI Text-Only Candidate Physical Acceptance (2026-08-29)

- The audited text-only candidate was written only at `0x2000` and passed
  both esptool write-time hash validation and an independent `verify-flash`.
  The erased/write range ended at `0x4BFFF`; the external model container at
  `0xE0000` was not accessed.
- Physical acceptance is confirmed by the user: the UI lights normally and
  the long-term press/release feedback remains correct (touched area darkens
  while pressed and restores on release).
- Accepted visible-UI image is now
  `.codex/nuttx-ui-textsafe-20260828.bin`, 300,172 B, SHA-256
  `cf4b03b0d61d04617b86cff29e910060277025b6e0e4395f78cf53c32bb6cf14`.
  The earlier `22e48e...e743` archive remains the rollback image. This
  acceptance advances only the workflow UI presentation; it does not claim
  that functional button binding is complete.

## Workflow Binding Architecture Audit (2026-08-29)

- The later source snapshot at `.codex/workflow-binding-20260829/` confirms
  that button hit-testing, label-before-defect gating, selected-stage drawing,
  and the callback seam `esp32p4_dsi_workflow_cb_t` already exist. The
  application receives `workflow_button_handler(stage)` after a valid release.
- The handler's eight branches are currently log-only placeholders; none yet
  invokes capture, gallery storage, TFLM inference, defect rendering, or
  position-deviation logic. Thus this is an implementation seam, not evidence
  of completed functional binding.
- The implementation is now explicitly staged: callback-to-worker task,
  controlled single-frame capture, LABEL/TFLM result unlock, then individual
  defect/energy/position displays. See
  `docs/handoffs/22-workflow-button-binding-plan-20260829.md`. The physical
  text/touch baseline remains frozen until a separately reversible functional
  candidate passes static and board acceptance.

## Correction: Current Screen Has No Physical Workflow Buttons (2026-08-29)

- User physical observation corrects the earlier interpretation: the accepted
  text-only image displays replacement text but **does not draw button boxes
  or expose the eight workflow touch targets**. The 105-byte candidate changed
  only strings in the existing renderer; it cannot create layout geometry or
  input behavior.
- Therefore the workflow UI/function-binding module remains at 45%, not 64%.
  The later callback-capable source is a design reference only and must not be
  represented as functionality present on the accepted board image.
- Next required work is a separately reversible candidate that first adds
  visible button geometry and matching hit areas while retaining the accepted
  press/release renderer, before attaching capture or inference behavior.

## Visible-Button Candidate: Offline Implementation (2026-08-29)

- A dedicated offline source snapshot is retained at
  `.codex/workflow-binding-20260829/`. It adds eight visible workflow buttons
  and uses exact 214x46-pixel hit rectangles. The 24-pixel horizontal gutters,
  8-pixel row gutter, and outer edges are deliberately inert.
- Host verification `python tools/verify_workflow_hit_regions.py` passed:
  all eight button centers map to their intended stages and all tested gutters
  map to no stage.
- The minimal button candidate compiled on the VM as 301,272 B, SHA-256
  `2364b55912612bae2947ec1fcf790ac4d8d6deb6691a5db432a1e9bbb2c30fcc`;
  its strings include `CAPTURE`. It changes 191,475 bytes within the first
  300,172 bytes relative to the accepted text baseline, so it is a full
  rebuilt image rather than a byte-local patch and has **not been flashed**.
- The external model container is untouched. Because prior full rebuilds
  black-screened, the next gate is an explicit candidate-risk decision and a
  prepared immediate rollback, not automatic board flashing.

## Static Full-Layout Candidate (2026-08-28)

- The archive's camera-diagnostic source provenance is now established:
  `apps/examples/camera_diag/camera_diag_main.c.orig` exactly matches the
  recovered local reference source (SHA-256
  `18f7543b424fc6295a4077a3b1b100bc0f78d1cffaf5d06b94eb6de4209e10a0`).
- An isolated VM worktree at `/home/max/openvela-p4-ui-static-candidate-20260828`
  restored the archive-era minimal `Makefile`, `Kconfig`, and `.config`. Its
  first clean reconstruction produced 299,984 B, proving the rich TFLM/ESP-DL
  link set was excluded. It differs from the archived 300,172-B image by 188 B
  because current board-source timestamps/revision metadata are not identical;
  it is therefore a build-proven source baseline, not a replacement archive.
- A static full-layout candidate was then built in that isolated worktree:
  300,284 B, SHA-256
  `3132d5ac13cfb506863110e8ba8f4c5e3962c50a80190cfdfe6433b4745485dd`.
  It draws the project title, workflow card, result placeholder, and the eight
  planned button labels. Static-scope checks prove it contains no workflow
  callback, camera, model, TFLM, ESP-DL, or inference bindings. It has **not**
  been flashed; the accepted press/release archive remains on the board.

## Static Full-Layout Board Rejection and Recovery (2026-08-28)

- The static full-layout candidate (300,284 B, SHA-256
  `3132d5ac13cfb506863110e8ba8f4c5e3962c50a80190cfdfe6433b4745485dd`)
  was written only at application offset `0x2000`, then passed both esptool
  write-time hash validation and standalone `verify-flash`.
- Physical acceptance failed: the user reported a black screen. The candidate
  is rejected and must not be reused as a display baseline, despite successful
  compilation and flash verification.
- Immediate recovery restored the accepted UI-only press/release archive at
  `0x2000`. Its write-time hash validation and standalone `verify-flash` both
  passed. The external model container at `0xE0000` was never written.

## Rebuild-Layout Isolation Test (2026-08-28)

- To isolate the static-layout failure, an otherwise untouched minimal-source
  control image was rebuilt from the archive-era `camera_diag_main.c.orig`,
  `esp32p4_dsi.c.pre-result-ui-20260822`, minimal `Makefile`, and original
  configuration. It was 299,984 B, SHA-256
  `6d27eebe219bbde13231882d8a5f8b349f63abe811846399a533715488e55d41`.
  Scope verification confirmed it contained no static full-layout string.
- The control image passed write-time validation and independent
  `verify-flash`, but the user again observed a black screen. This is a
  physical, red-capable differential result: rebuilding under the current
  tree/toolchain is itself sufficient to regress display, independently of
  extra UI drawing.
- The board was immediately restored to the accepted 300,172-B archive,
  SHA-256 `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`,
  at `0x2000`; both write-time and standalone verification passed. Do not
  flash any image rebuilt in `/home/max/openvela-p4-ui-static-candidate-20260828`
  until its image-level difference from the historical archive is explained.

## Stable-Archive Reproduction Audit (2026-08-28)

- Continued read-only diagnosis against the frozen archive and rejected minimal
  rebuild. Both images use the same ESP32-P4 image header, entry point
  (`0x4ff45d5a`), three RAM-load segments, segment addresses, and segment
  lengths. The VM still uses documented ESP `riscv32-esp-elf-gcc 16.1.0`.
- Segment 0 is byte-identical; the two executable RAM segments differ at only
  57 byte positions. These map to generic early boot/clock/interrupt/static
  initialisation paths (`irq_dispatch`, scheduler unlock, RTC clock setup,
  `__esp_start`, and static-data relocation), not the DSI UI start or GT911
  feedback functions.
- This narrows the reproduction failure but is not yet a safe byte-level fix:
  the flash-mapped content also has layout/relocation differences. No board
  write occurred; the accepted UI image remains at `0x2000` and `0xE0000`
  remains untouched.
- Next: trace each early-start difference through link-map and generated build
  inputs, then reproduce the archived startup bytes before a new button build.

## Stable-Archive Startup Root Cause (2026-08-29)

- A same-source/same-config double-build check shows executable image content
  is deterministic: two consecutive rebuilds have byte-identical RAM-load
  segments; only one build timestamp byte changes in the full image.
- The minimal black-screen control was therefore not a valid equivalent of the
  stable archive. Its `.config` enables
  `CONFIG_EXAMPLES_CAMERA_DIAG_AUTOSTART_STATUS_UI`, but the board source that
  actually compiled was an empty `esp32p4_boot.c`, so it never created the
  `openvela_ui` task or invoked `camera_diag --dsi-ui-live`.
- The archived working image contains `openvela_ui` and `task_create failed`
  strings, matching the recovered archive-era `esp32p4_boot.c` whose late-init
  task waits for panel settling and runs `camera_diag --dsi-ui-live`. This is
  a concrete startup-path mismatch that explains a black screen independently
  of the DSI/touch renderer.
- A separate VM worktree `/home/max/openvela-p4-ui-autostart-repro-20260829`
  was created for a no-flash reproduction using the recovered board boot file.
  The physical board was not written; `0x2000` remains the accepted UI image
  and `0xE0000` remains untouched.

## Automatic-Startup Reproduction Candidate (2026-08-29)

- Corrected an additional build-path issue: the Make build compiles through
  `arch/risc-v/src/board`, a symlink into the shared live integration tree,
  rather than the copied board-source path. The archive-era boot source was
  restored at that actual compilation path, its board object/library was
  invalidated, and the candidate rebuilt.
- Static acceptance passed: the rebuilt 300,172-byte image has the same image
  header, entry point, segment addresses, and **byte-identical three RAM-load
  segments** as the accepted archive. It also contains `openvela_ui`, its
  task-create diagnostic, `camera_diag --dsi-ui-live`, and the accepted DSI
  touch-feedback symbols.
- Candidate `nuttx.bin` SHA-256 is
  `cfca93d49a751fb08e75edd80e2c253f737b5aa2bf3d730706ffcd76ea4d0c59`.
  It was written only to application offset `0x2000` on 2026-08-29; esptool
  write-time hash validation and standalone `verify-flash` both passed. The
  erased/write range ended at `0x4BFFF`; the external model container at
  `0xE0000` was not modified. Physical screen/touch acceptance is pending
  user observation; immediately restore the accepted archive if it fails.

## Automatic-Startup Candidate Board Rejection and Recovery (2026-08-29)

- Physical acceptance failed: the user reported a black screen after the
  candidate booted. The candidate is rejected and must not be reused.
- The accepted archive (`22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`)
  was immediately restored only at `0x2000`. Both write-time hash validation
  and standalone `verify-flash` passed; the write range was `0x2000–0x4BFFF`
  and `0xE0000` was untouched.
- The user then confirmed the recovered board is normal again. The stable
  visible UI and long-term press/release feedback baseline is re-accepted and
  frozen.
- Important correction: matching all RAM-load segments, startup entry, and
  visible startup strings is necessary but insufficient. Flash-mapped text/
  rodata layout affects the running image, so no future rebuilt candidate may
  be board-tested without an exact whole-image provenance/reproduction check.

## M16 Target Batch 02 (2026-08-23)

- The sample-resource dependency was corrected: `m13_validation_samples.o` now depends on all three `.rgb565` inputs. The prior unchanged-binary run is excluded.
- The verified application, 839,476 B / SHA-256 `44eebe94...0d6382`, was written only at `0x2000` and passed both write-time hash verification and `verify-flash`.
- Three additional images containing class 7 `wrinkle` were executed entirely on the ESP: `2026-03-05_17-30-00_605` hit class 7; `2026-03-05_17-33-39_283` and `2026-03-05_17-39-12_837` missed class 7 while retaining their class 5 `stain` hit. Together with the first class-7 sample, target class-7 presence is 1 hit / 4 samples.
- Evidence: `evidence/m13-esp-wrinkle-batch-02-fixed-20260823-serial.log`. Class 7 does not meet the no-miss acceptance rule; the next work is target confidence/threshold and quantized-output parity analysis before expanding the batch further.

## M16 ESP-Equivalent Host Simulation (2026-08-23)

- `tools/m16_simulate_esp_tflite.py` runs the deployed `best_full_integer_quant.tflite` with the board's RGB565 restoration, input quantization, raw score threshold, same-class 45% IoU NMS, and defect policy.
- On all four RGB565 assets already executed on the ESP, defect-class presence matched the board exactly: the first sample produced only class 5; batch-02 produced classes 5+7, then class 5 only, then class 5 only. This localizes the class-7 misses to the deployed INT8 model/common pipeline rather than an ESP-only runtime fault.
- Evidence: `evidence/m16-esp-equivalent-four-wrinkle-20260823.json`. Raw candidate scores have small host/target numeric differences, so this proves semantic parity on these samples, not bit-for-bit operator parity.

## M16 Threshold Candidate (2026-08-23)

- ESP-equivalent sweep over all 285 validation images found raw threshold `-10` improves image-level recall versus the current `-5`: stain `271/285` to `264/285` at the lower threshold, damage `22/38` to `30/38`, and wrinkle `30/36` to `32/36`. The lower threshold still does not achieve no-miss wrinkle acceptance, but it is the current board candidate.
- Candidate application changed only the decoder threshold from `-5` to `-10`; build size is 839,480 B, SHA-256 `1b3e2e79aa154a0e3f996770aa08c0b186d7bb9782b398ab1274a03444927777`. It was written only at `0x2000`; write-time and standalone flash verification passed.
- Board re-test has started. The first sample confirms `threshold_raw=-10` and detects both `stain` and `wrinkle`; remaining two samples are awaiting serial-log retrieval because the SSH approval proxy is temporarily returning 503/429.
- Evidence: `evidence/m16-threshold-sweep-20260823.json`. Do not mark the candidate accepted until the remaining board logs are read and compared.

## M16 Decoder Parameter Optimization (2026-08-23)

- A fast ESP-equivalent targeted search preserved the target's per-anchor top-class rule, first-seen candidate cap, and same-class NMS. It compared global defect thresholds `-20/-15/-10/-5`, candidate caps `16/32/64`, and NMS IoU `0.30/0.45/0.60` over all 285 validation images.
- Best candidate is defect thresholds `stain=-20`, `damage=-20`, `wrinkle=-20`, candidate cap `32`, NMS IoU `0.45`. Image-level class-presence recall is stain `285/285` (100%), damage `32/38` (84.21%), wrinkle `32/36` (88.89%). Cap 64 does not improve this result; NMS IoU has no observed effect for this candidate.
- This is a major decoder improvement over the frozen `-5/cap16` baseline, but it still cannot satisfy no-miss damage/wrinkle acceptance. The remaining misses require a model/decode-architecture change, such as preserving per-class scores rather than discarding non-top classes at each anchor.
- Evidence: `evidence/m16-esp-decoder-targeted-optimization-20260823.json`; tool: `tools/m16_optimize_esp_decoder_fast.py`.

## M16 Original Model, All Labels (2026-08-23)

- The original `models/best.pt` was evaluated on all 285 validation images at 320 px, confidence 0.65, IoU 0.45. For each of all ten labels, a labeled image counts as a hit when at least one prediction of that class exists; this is image-presence recall, not box-level mAP.
- Results: level_1 `44/45` (97.78%), level_2 `60/60` (100%), level_3 `61/62` (98.39%), level_4 `63/68` (92.65%), level_5 `49/50` (98.00%), stain `285/285` (100%), damage `34/38` (89.47%), wrinkle `34/36` (94.44%), label `285/285` (100%), box `25/29` (86.21%).
- The original model still has misses in every category except level_2, stain, and label. Its damage/wrinkle result exceeds the current best ESP-equivalent decoder result (`32/38`, `32/36`), but the comparison includes a different source model/runtime and 320px RGB rather than the deployed 256px RGB565 INT8 path.
- Evidence: `evidence/m16-original-bestpt-all-labels-20260823.json`; tool: `tools/m16_original_model_all_labels.py`.

## M16 Matched Float32 vs INT8 Audit (2026-08-23)

- A matched export pair from `artifacts/model-int8-20260821/best_saved_model` was audited: both models use input `1x320x320x3` and output `1x14x2100`; one is float32 and the other full INT8. Both were run through the same 320px RGB565 round-trip input and the same 0.65 / 0.45 class-aware NMS image-presence evaluation.
- Float32 versus INT8 results: damage `37/38` (97.37%) vs `36/38` (94.74%); wrinkle `34/36` (94.44%) vs `33/36` (91.67%). Other INT8 results remain close: level_1 `44/45`, level_2 `60/60`, level_3 `61/62`, level_4 `63/68`, level_5 `49/50`, stain `285/285`, label `285/285`, box `23/29`.
- Conclusion: full INT8 quantization is not the principal reason the currently deployed 256px model is below the original-model result. A 320px full-INT8 model is the leading deployment candidate for restoring original-model capability; validate TFLM memory/latency and preserve a model-container rollback capture before modifying `0xE0000`.
- Evidence: `evidence/m16-float-int8-320-rgb565-compare-20260823.json`; tool: `tools/m16_compare_float_int8_320.py`.

## Position-Deviation Dataset Audit (2026-08-23)

- The original specialized runner `P/test_position_deviation.py` and historical outputs `P/outputs/test_results/latest_position_deviation.json` remain. The runner recorded 111 images on 2026-05-27, but its configured source directory `C:\Users\ASUS\Desktop\位置偏差good` is now absent.
- The retained JSON records model predictions (84 position-deviation decisions among 86 images with both label and box), not independently labeled normal/deviation ground truth. It is therefore useful for reproducing the old run but cannot establish position-deviation accuracy or acceptance.
- Position-deviation target validation remains blocked until the original image directory or a labeled replacement set is recovered.

- Diagnosis established that manual `camera_diag --dsi-ui-live` immediately
  restores visible output, while the old automatic task starts
  `--touch-ui-live` directly after a one-second delay. This couples DSI
  bring-up and the persistent GT911 polling loop during cold startup.
- The recovery build starts `--dsi-ui-live` after two seconds, then starts
  `--touch-ui-live` as a separate task after five seconds. The unchanged
  touch feedback and DSI drawing paths remain intact.
- The OpenVela/NuttX build completed successfully. Candidate application:
  439,388 B, SHA-256
  `4ae011e91396959ade3db6decf36ebcdfb73cc72d8c3a53ef350a4f2efbb3569`.
  Evidence: `evidence/m05-autostart-split-build-20260823.log`. It is not yet
  flashed or accepted on the physical board.

1. Every implementation, validation, or blocker changes the corresponding row
   in this file before its progress percentage is reported to the user.
2. Progress may increase only with an evidence path, successful build, device
   log, or direct physical-board confirmation.
3. Completed modules are not reopened merely because a dependent module fails.
   A regression must be explicitly observed and recorded before changing their
   status.
4. Every user-facing status reply must include the module percentages listed in
   the Module Register, at least in compact form.

## Workflow Candidate Safety Gate (2026-08-29)

### 自动启动参数修复探针（2026-08-30）

- 已在独立 VM 工作区 `openvela-p4-ui-autostart-argvfix-20260830` 重建候选。
  历史正确实现表明 `task_create()` 的参数数组不得重复放入程序名；修复后 UI
  任务参数为 `--dsi-ui-live`，触摸任务参数为 `--touch-ui-live`。
- 候选 `artifacts/ui-autostart-argvfix-20260830.bin`：300,196 字节，SHA-256
  `336624b52906223043db80e1760808e60c252ee703d53a81b26000ff6c1fbf10`；
  `verify_esp32p4_ui_boot_equivalence.py` 通过。
- 已执行一次仅应用区 `0x2000` 的自动回滚实机探针。UART 已确认：`NuttShell`、
  `camera_diag: starting persistent status UI`、`dsi: persistent status UI active`
  与 `touch_diag: GT911 product=911`。此前的 `camera_diag: unknown option`
  未再出现。
- 探针退出后已自动恢复冻结稳定镜像；回读 SHA-256 为
  `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`，
  与基线逐字节一致。`0xE0000` 模型容器未访问。
- 结论：自动启动路径已通过启动级验证，尚欠一次用户可见性和持续触摸的实屏验收；
  在该验收前不得将其标记为完成，也不得替代冻结稳定镜像。

### 自动启动持续运行部署（2026-08-30）

- 用户已授权继续后，已将通过探针的 `ui-autostart-argvfix-20260830.bin` 写入应用区
  `0x2000`；写入与 `verify-flash` 均通过（SHA-256
  `336624b52906223043db80e1760808e60c252ee703d53a81b26000ff6c1fbf10`）。
- 写后 UART 再次确认 `NuttShell`、持久 UI 启动、DSI UI 激活和 GT911 触摸识别；
  未出现未知参数错误。模型容器 `0xE0000` 未访问。
- 当前板端为该持续运行候选，等待实屏确认画面常亮及按下变深、松开复原反馈。

### 真实按钮流程接入审计（2026-08-30）

- 当前持续运行镜像经源码核对仅提供 GT911 坐标到全屏按压视觉反馈；没有按钮矩形、
  命中测试、回调或业务状态机。因此当前“触摸反馈正常”不能等同于“按钮可用”。
- 已复核历史工作流候选的可复用部分：`workflow_state` 提供拍摄、图库、标签、能效、
  污渍、破损、褶皱、位置偏差八个步骤的前置条件、忙碌锁与操作编号；GT911 在松开后
  才由工作线程执行耗时任务，避免阻塞按下/松开的显示反馈。
- 但该历史候选的 DSI 提交路径仍调用逐帧 `dsi_submit_status_ui_frame()`；它属于此前
  已判定会造成闪烁/黑屏的高风险差异，不能直接部署或移植到当前持续运行镜像。
- 下一实现步骤：从冻结、已通过持久触摸验收的 DSI cache-sync 路径中抽取按钮命中与
  状态机接口，先建立主机侧命中区/前置条件测试，再构建独立候选并执行启动等价性和
  自动回滚探针；在这些检查完成前禁止刷写。

### 按钮状态机安全迁移预检（2026-08-30）

- 已在 VM 独立副本完成 `workflow_state` 宿主机单元测试：覆盖拍摄前禁止标签、
  标签前禁止缺陷步骤、忙碌锁、过期完成回调忽略，以及标签完成后位置偏差步骤放行；
  结果 `workflow_state_test: PASS`。
- 已验证迁移方案可删除所有 `dsi_submit_status_ui_frame()` 调用，仅保留当前稳定路径
  的局部 cache-sync；源码门禁确认八按钮布局、命中回调、松开后工作线程与状态机
  源文件均存在。
- 构建前补齐隔离副本时 VM 根分区空间耗尽（可用空间降至 149MB），构建被配置文件/源树
  复制中断，未生成候选镜像，未刷写板卡。已删除仅本次创建且未完成的
  `openvela-p4-button-workflow-safe-20260830` 临时副本，释放约 586MB；其余既有工作区、
  稳定镜像和模型容器未修改。
- 当前阻塞：VM 仅余约 734MB，无法安全完成完整 NuttX 隔离构建。下一步需先做可审计的
  VM 空间清理或扩容；空间恢复后重复独立构建、静态等价性审计和自动回滚探针。

### VM 空间恢复与按钮候选重建（2026-08-30）

- 已完成只读占用审计并清理六个已在台账中明确拒绝、且不含唯一稳定基线的历史候选目录：
  `min-workflow-core`、`tail-hook-probe`、`workflow-link-placement`、
  `ui-static-candidate`、`wholeimage-repro` 与 `wholeimage-repro2`。VM 可用空间由约
  734MB 恢复至约 9.8GB。稳定恢复目录 `dwgdma-api-probe`、当前持续运行的
  `ui-autostart-argvfix`、模型容器和板端应用区均未删除或写入。
- 新的完整隔离候选 `openvela-p4-button-workflow-safe-20260830` 已建立；确认其板级
  源码为私有副本。已将八按钮布局、按钮命中回调、松开后工作线程及 `workflow_state`
  状态机迁入，并删除全部 `dsi_submit_status_ui_frame()` 调用，仅保留 cache-sync
  刷新路径。状态机宿主机测试继续通过。
- 完整 NuttX 构建已通过按钮/DSI 编译阶段，但在链接阶段被既有 TFLite Micro 静态库依赖
  缺失阻断（`MicroPrintf`、`Register_CONV_2D` 等未解析）。这是旧工作流候选自身的
  TFLM 构建清单不完整，非按钮状态机或触摸代码错误。未生成可用镜像，未刷写板卡。
- 下一步：从已完成的 TFLM 集成工作区恢复完整 `tflite-micro` 链接依赖，或将本阶段明确
  切为不引入 TFLM 的按钮占位工作流；两者均须先完成离线链接和安全审计。

### 无模型按钮交互候选审计（2026-08-30）

- 已确认当前稳定配置未启用 `CONFIG_TFLITEMICRO`；直接搬入旧完整推理主程序会导致
  TFLM 未解析符号，不能作为本轮按钮验证基础。因此已构建无模型按钮候选：八个可见
  按钮、命中回调、松开后状态转换、拍摄/标签前置条件均存在，但不执行相机或推理，
  不会把状态演示冒充为识别结果。
- 候选已成功离线生成：`artifacts/ui-button-workflow-no-model-20260830.bin`，302,280
  字节，SHA-256 `3d29400963609018d54861e69e7cf14a282c5b82bd13a1d744cd7549ef3c35ae`。
  源码检查确认不存在 `dsi_submit_status_ui_frame()`；未发现按键路径中的 DSI stop/start。
- 但启动等价性门禁失败：RAM 段从基线末段 `4416` 字节变为 `4428` 字节。进一步检查还
  显示迁入旧 DSI 实现会将 DRAM BSS 从 22,404 字节扩大到 37,556 字节；即使按钮自身
  只新增少量状态，也不能满足冻结显示路径的安全约束。
- 决策：该候选禁止写入板卡，模型容器 `0xE0000` 未访问。下一步必须从当前稳定 DSI
  源码最小增量实现命中区域，不再整体替换为旧 DSI 工作流版本，并要求 RAM/启动段门禁
  通过后才可进入自动回滚探针。

### 最小四按钮命中候选与自动回滚启动验证（2026-08-30）

- 基于当前持续运行的 `ui-autostart-argvfix-20260830` 源，仅在
  `esp32p4_dsi.c` 的既有 cache-sync 触摸路径中增加四个底部可见按钮和松开命中日志：
  `CAPTURE`、`GALLERY`、`LABEL`、`INSPECT`。不引入全局工作流状态、相机、模型或
  `dsi_submit_status_ui_frame()`；源码门禁通过。
- 候选 `artifacts/ui-button-minimal-hit-20260830.bin` 已离线构建，300,532 字节，
  SHA-256 `1f93a18c773fc620378c853350401ac84d9246311ee1d782f4230c1956a52827`。
  与冻结触摸基线的 RAM 头和段表等价性检查通过。
- 已执行一次完整自动回滚启动探针：候选仅临时写入应用区 `0x2000`，`verify-flash`
  通过；UART 捕获到 `NuttShell (NSH)`、`camera_diag: starting persistent status UI`、
  `dsi: persistent status UI active`、`touch_diag: GT911 product=911`。探针结束后无条件
  恢复当前自动启动稳定镜像 `ui-autostart-argvfix-20260830.bin`（300,196 字节，SHA-256
  `336624b52906223043db80e1760808e60c252ee703d53a81b26000ff6c1fbf10`）；独立
  `verify-flash`、300,196 字节回读及逐字节比较均通过。
- 该探针未做人工可见性或四个命中区实屏验收，因此候选尚未部署为常驻镜像；模型容器
  `0xE0000` 未读写。下一步必须在用户可观看时运行同一候选的暂时验收并自动恢复，确认
  按钮可见且长时触摸反馈未回退后，才可考虑常驻部署。

### 最小四按钮候选常驻部署（2026-08-30）

- 经用户授权，`ui-button-minimal-hit-20260830.bin` 已写入应用区 `0x2000`；写入时
  哈希校验与单独 `verify-flash` 均通过。随后 UART 重启采集确认 `NuttShell`、持久状态
  UI、DSI 激活与 GT911 均正常启动。
- 当前待用户实屏验收：底部四个可见按钮是否出现，并确认按住变深、松开恢复的长期触摸
  反馈仍正常。未完成该验收前，不得宣称按钮流程已完成。模型容器 `0xE0000` 未访问。

### 四按钮“可见但不可点”根因与单一所有者修复（2026-08-30）

- 根因已确认：原常驻四按钮镜像只自动启动 `--dsi-ui-live`，因此 UI 能画出，但没有
  运行 GT911 轮询，触摸回调和按钮命中不会发生。不能再额外启动一个独立触摸任务，因为
  不同任务地址空间无法共享 DSI framebuffer 状态，正是此前触摸过一会失效/闪烁的来源。
- 修复候选 `artifacts/ui-button-singleowner-touch-20260830.bin`：保留原有按钮、绘制和
  cache-sync 反馈路径，启动时仅创建一个任务，以 `--touch-ui-live` 同时拥有 DSI 与 GT911。
  大小 300,532 字节，SHA-256
  `094bdc0751dc390e0577c9ebd420f223ccf29d656da479b77fa63436fe4fad06`；与四按钮候选
  启动段结构等价性检查通过。
- 自动回滚实机探针确认：持续 GT911 轮询、按下变深/松开恢复均启动，且 UART 实收
  `button: LABEL pressed`、`button: INSPECT pressed`、`button: GALLERY pressed`。
  探针随后恢复原四按钮镜像，300,532 字节回读与逐字节比较通过。
- 经验证后，单一所有者修复镜像已常驻写入 `0x2000`，写入哈希验证与独立
  `verify-flash` 均通过。模型容器 `0xE0000` 未读写。当前待用户确认四按钮的长期实屏
  可点性；在此之前，按钮仅具真实命中事件，尚未绑定相机或识别业务。

### 按钮可见反馈修复（2026-08-30）

- 用户正确指出“串口命中”不等于按钮有作用：此前只有触摸点附近的小圆形变深，四个按钮
  自身没有可见状态变化，也未接入业务动作，因此不能作为真实按钮体验验收。
- 首个整按钮绘制实现因辅助枚举/抽象使 RAM 段增加 16 字节，被启动等价性门禁拒绝，未写入
  板卡。随后重写为无状态直接分支：按下命中区时仅 cache-sync 对应 210×52 按钮矩形并绘制
  更深的同色；松开时立即以原色重绘该按钮。非按钮区域继续使用原局部圆形触摸反馈。
- `artifacts/ui-button-fullpress-noram-20260830.bin`：301,116 字节，SHA-256
  `b5436e655637c6b9218eb701fdccfce1592c3f8526e86484c4812d6216ebad0d`，相对单一所有者
  触摸版的 RAM 启动段表等价性检查通过。已常驻写入 `0x2000`，写入哈希验证和独立
  `verify-flash` 均通过。用户已实屏确认：按哪个按钮，哪个按钮整体变深，松开恢复。
  模型容器 `0xE0000` 未访问。

### 第一项业务按钮绑定：CAPTURE 请求状态（2026-08-30）

- 按钮反馈已由用户确认完成。下一步先建立不阻塞 GT911 的真实 UI 业务事件边界：点击
  `CAPTURE` 并松开后，中心状态区显示 `CAPTURE REQUESTED`。这是请求已从触摸层进入 UI
  业务层的可见证据，但**尚不代表真实相机帧已采集**。
- 首次为该候选创建完整隔离副本时 VM 可用空间降至约 1.2GB，因此在构建前停止并删除了仅本次
  未完成副本；板端和既有候选均未受影响。随后改用带 `trap` 自动恢复源文件的低空间构建方式，
  构建后离线树已复原。
- `artifacts/ui-button-capture-status-20260830.bin`：301,192 字节，SHA-256
  `5b829fd7bf15effc6ea55fa18ab4b2526e19611127add2729f48bebcf19eb529`，对当前按钮反馈版
  的启动段等价性检查通过。已写入 `0x2000`，写入哈希校验与独立 `verify-flash` 均通过。
  待用户实屏点击 `CAPTURE` 验收状态文字；下一阶段才是将请求交给独立工作任务做真实单帧采集。
  模型容器 `0xE0000` 未访问。

### CAPTURE 真实单帧采集接入（2026-08-30）

- 在保持单一 DSI/GT911 所有者的前提下，新增 `camera_diag_capture_one_rgb565_for_ui()`：
  复用已验证的 SC2336→CSI→ISP RGB565 单帧采集链，采集函数不调用 DSI 预览、启动或停止，
  帧写入既有 PSRAM `g_camera_frame_rgb565`。
- `CAPTURE` 松开时先显示 `CAPTURING`，采集成功显示 `CAPTURE COMPLETE`，失败显示
  `CAPTURE FAILED`；这是实际相机采集状态，不是串口 stub。采集期间触摸轮询会等待该次
  硬件操作结束，完成后恢复。
- 候选 `artifacts/ui-button-capture-real-20260830.bin`：301,632 字节，SHA-256
  `66c3990aa15e3ad9ada221589889075d492494f7c64f2057f335e501b5a47af9`；启动段等价性通过，
  已写入 `0x2000`，写入哈希、独立 `verify-flash` 和启动指纹（NuttShell、DSI、GT911）均通过。
  待用户在镜头前点击 `CAPTURE`，确认真实采集后的屏幕状态；模型区 `0xE0000` 未访问。

### CAPTURE 点击后蓝屏回滚（2026-08-31）

- 用户实测点击 `CAPTURE` 后出现蓝屏，说明“在持续 DSI/GT911 所有者线程内直接初始化
  CSI/ISP 并采集”不可接受，不能继续保留该候选。
- 已将应用区 `0x2000` 回滚到上一个已确认可见的 `CAPTURE REQUESTED` 镜像
  `artifacts/ui-button-capture-status-20260830.bin`（301,192 字节，SHA-256
  `5b829fd7bf15effc6ea55fa18ab4b2526e19611127add2729f48bebcf19eb529`）；写入校验和
  独立 `verify-flash` 均通过。模型区 `0xE0000` 未访问。
- 初步根因：CSI/ISP 的 DW-GDMA/中断与持续 DSI 的 DW-GDMA/中断共享底层资源；在同一
  触摸任务中启动 CSI/ISP 会破坏显示刷新状态。后续必须采用资源仲裁明确的独立采集阶段：
  先安全暂停/恢复 DSI（或使用不争用的采集通路），并在离线与可回滚实机探针通过后再刷写。
  当前真实采集功能退回“未部署”，不得宣称可用。

### CAPTURE 资源仲裁候选（2026-08-31）

- 根因约束已固化：持续刷新的 DSI 与 SC2336→CSI→ISP 采集不能并发占用
  DW-GDMA/中断资源。候选改为：`CAPTURE` 松开后完整停止 DSI UI，独占完成一帧
  RGB565 采集，再重启 DSI UI；采集期间的短暂黑屏是预期的资源切换。
- 候选镜像为 `artifacts/ui-button-capture-pause-dsi-20260831.bin`，301,604 字节，
  SHA-256 `24ad92befa0777e04fdcbc701814756161d38de6152dffbfd6754534678a9149`。
  构建采用临时原地改动和 `trap` 恢复；构建后远端源树已确认恢复。候选启动段等价检查
  `PASS`。
- 已执行一次自动回滚启动探针：候选到达 `*** Booting NuttX ***` 与 DSI 状态 UI 初始化；
  探针随后把 `ui-button-capture-status-20260830.bin` 恢复到 `0x2000`，301,192 字节读回
  SHA-256 与稳定镜像完全相同。探针没有访问模型区 `0xE0000`。
- 经写入哈希校验和独立 `verify-flash` 成功后，候选已部署到应用区 `0x2000`，供一次人工
  实屏验收。**当前待验收：** 点击 `CAPTURE` 后是否短暂黑屏、恢复到 UI、中央显示采集
  完成/失败状态，且随后四个按钮仍能持续触摸。若任一项失败，立即恢复
  `ui-button-capture-status-20260830.bin`；不得重刷已知会蓝屏的
  `ui-button-capture-real-20260830.bin`。

#### 实屏恢复结果（2026-08-31）

- 用户已点击一次 `CAPTURE`，实屏结果为“恢复正常”。这证明暂停 DSI、执行独占采集阶段、
  重新启动 DSI UI 的恢复路径未出现此前的蓝屏或永久黑屏。
- 本次后台 UART 观察没有收到有效字节，故不能仅凭该次日志宣称帧长度或采集结果码已确认。
  下一项验收是：恢复后任点一个其他底部按钮，确认按下变深、松开复原仍持续有效；然后再
  增加一个 UI 可见的采集结果状态，作为不依赖串口的采集完成证据。

#### 采集结果可见化候选（2026-08-31）

- 新候选在采集结束后保留返回码，并在重启 DSI 后的中央状态区显示：成功为
  `CAPTURE COMPLETE / FRAME READY`，失败为 `CAPTURE FAILED / CHECK CAMERA`。该状态
  由实际采集函数的返回值决定，不是固定文案。
- 镜像：`artifacts/ui-button-capture-pause-dsi-result-20260831.bin`，301,712 字节，
  SHA-256 `dbf3635abfc5e1fd3ae7eb6e2c7ee9bb163d5a13d91f65423a5c289257d4a938`。相对
  上一暂停-恢复候选的启动等价检查 `PASS`；远端原地构建的临时源已自动恢复。
- 已写入应用区 `0x2000`，写入哈希校验和独立 `verify-flash` 均成功；`0xE0000`
  模型容器未访问。等待用户点击 `CAPTURE` 进行实屏验收。

#### 采集结果实屏验收（2026-08-31）

- 用户点击 `CAPTURE` 后，恢复的 UI 显示 `FRAME READY`。该文案只在
  `camera_diag_capture_one_rgb565_for_ui()` 返回完整 RGB565 帧时设置，故已确认
  SC2336→CSI→ISP 单帧采集成功，帧数据已写入板端 PSRAM 缓冲。
- 下一步是将这张已采集帧以低风险只读方式接入 `GALLERY` 的最小预览；模型推理、
  多图管理和缺陷标注仍未接入，不能混同为完成。

#### 图库最小预览候选（2026-08-31）

- `GALLERY` 的最小行为已实现为：仅在 `FRAME READY` 后可用；点击后停止状态 UI，
  以全屏 RGB565 方式显示已保存在 PSRAM 的最近一张采集帧约 3 秒，再重新启动状态 UI。
  没有重新启动 CSI/ISP，不会和 DSI 并发。
- 候选镜像：`artifacts/ui-gallery-preview-20260831.bin`，301,804 字节，SHA-256
  `454c205d67aa76f61e7a46273cce398a7e71bfe3aebc721ac4a57e8cbe76b61e`；相对于采集
  结果版的启动等价检查 `PASS`，远端临时源已恢复。初次构建发现并修复了候选缺少安全
  采集函数链接依赖的问题；未产生失败镜像或板端写入。
- 已写入 `0x2000`，写入校验和独立 `verify-flash` 均成功，`0xE0000` 未访问。等待
  用户实屏顺序验收：`CAPTURE` → `FRAME READY` → `GALLERY` 全屏预览并自动回到 UI。

#### 图库预览实屏验收（2026-08-31）

- 用户已按完整顺序验收，结果为“有预览并恢复”：`CAPTURE` 后生成帧，`GALLERY`
  全屏显示最近采集画面，并自动返回原 UI。该闭环已完成；不存在蓝屏、永久黑屏或不能
  恢复的现象。
- 下一步切换到 `LABEL`：以已采集帧为前置条件，先建立标签/能效等级识别的明确 UI
  状态与结果边界，再接入已验证的板端模型运行时。不得为模型工作回退当前通过的
  采集、图库、显示或触摸路径。

#### LABEL 前置条件与可见状态（2026-08-31）

- 先前用户在 `FRAME READY` 后点击 `LABEL` 没有反应的原因已澄清：当时板上仍是
  图库预览镜像，LABEL 候选尚在构建，功能尚未部署。
- 新镜像 `artifacts/ui-label-gate-20260831.bin`（302,020 字节，SHA-256
  `2135da9d914d6dc39701068aabc4d4c04e4ec53837ea7835c6a0ed3d402e5dc1`）将 LABEL
  与真实采集前置条件绑定：无帧时显示 `CAPTURE BEFORE LABEL`；有完整帧时显示
  `LABEL MODEL PENDING`。这仍不是模型推理结果。
- 对图库版启动结构等价检查 `PASS`，远端构建临时源已恢复；已写入 `0x2000`，
  写入校验和独立 `verify-flash` 均成功，模型区 `0xE0000` 未访问。等待用户实屏验收。

#### LABEL 流程实屏验收（2026-08-31）

- 用户确认：`CAPTURE` 后出现 `FRAME READY`，再点 `LABEL` 显示
  `LABEL MODEL PENDING`；此前已完成的按钮反馈、图库预览及 UI 恢复均保持正常。
- 拍摄→预览→标签入口闭环现已验收。下一阶段是模型运行时接入前的离线容器、
  输入输出契约与最小 Invoke 路径审计；在审计通过前不得改动当前已验收 UI 镜像或
  模型容器。

### VSYNC + 流程候选实机拒绝与恢复（2026-08-29）

- 候选 `cdf11fd69ec60a157376e4f38154dc83135612496b0407a8b6f9b01843e42f14`
  曾仅写入 `0x2000`，写入哈希和 `verify-flash` 均通过，但用户实屏观察为黑屏。
  因此该候选被拒绝，禁止再次刷写。
- 已立即恢复验收稳定镜像
  `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`
  （300,172 字节）到 `0x2000`；写后哈希与独立 `verify-flash` 均通过。
- 外置模型容器 `0xE0000` 未被读取或写入。稳定 UI/触摸基线保持冻结。

### 黑屏启动指纹反馈环（2026-08-29）

- 已确认 `/dev/ttyACM0` 仅为下载/复位通道；板端 UART0 控制台为
  `/dev/ttyUSB0`，115200 8N1。此前用下载口抓启动日志会误得空输出。
- 新增 `tools/capture_openvela_boot_uart.py`：在 esptool 复位期间抓取 UART0，
  并判定 `Booting NuttX`、`NuttShell`、`persistent status UI active` 与
  `GT911 product=911` 四个启动指纹。
- 已对恢复后的稳定镜像实测：`reset_exit=0`、`uart_bytes=2522`，四个指纹均为
  `1`。后续候选必须先采集该日志，再进行实屏观察；禁止以无串口证据的黑屏
  候选重复刷写。

### 黑屏候选启动形状审计（2026-08-29）

- 已对稳定归档和黑屏流程候选进行真实 ESP 镜像段解析。稳定归档入口为
  `0x4ff45d5a`，RAM 负载段为 `0x4ff40000/0x60b0` 与
  `0x4ff46100/0x1140`；黑屏候选入口变为 `0x4ff48906`，对应段增至
  `0x4ff40000/0x8c60` 与 `0x4ff48c80/0x1484`，SPI 头字段也不同。
- 候选 ELF 同时使 flash text/rodata、IRAM 和 RTC 保留段布局发生变化，因此它
  不是可与稳定 DSI 启动路径等价的镜像；这一结论与实机黑屏一致。
- 新增 `tools/verify_esp32p4_ui_boot_equivalence.py`。稳定镜像自检 PASS；
  黑屏候选 `cdf11fd...` 自动 FAIL。未来任何 UI 候选未通过该门禁不得刷写。

### 稳定镜像可扩展性边界审计（2026-08-29）

- 稳定镜像当前未启用 `CONFIG_FS_ROMFS`、`CONFIG_ELF`、`CONFIG_NXFLAT` 或
  `CONFIG_MODULE`，因此不能在不重建启动镜像的条件下加载第二个 UI 应用。
- 唯一已证明保持启动形状的变更是 `.codex/nuttx-ui-textsafe-20260828.bin`：
  与稳定镜像同为 300,172 字节，启动等价检查 PASS，文本白名单检查 PASS
  （仅 105 字节改动，范围 `0x1091D..0x1099B`）。它只能替换现有静态文案，
  不能增加按钮图形、触摸命中区或功能绑定。
- 结论：在冻结镜像上不能诚实地实现“可见、可点击的八个按钮”。完整按钮功能
  仍需采用新的可恢复系统架构，并以 UART 启动指纹和实屏回滚循环逐步验证。

### DSI 重定位敏感性审计（2026-08-29）

- 已排除 Flash 容量头作为唯一原因：此前黑屏的静态布局候选与稳定镜像同为
  4MB / DIO / 80MHz，入口、RAM 段地址和段长度也一致，但实屏仍黑。
- 该候选中 `esp32p4_dsi_pattern_diag`、按压和松开函数的符号大小未变，却因
  全局链接布局改变而整体移动约 42KB。反汇编归一化对比的哈希不同：调用目标、
  字符串地址和 GP 相对全局状态偏移均被重新定位，故机器码不是稳定版的字节等价
  副本。
- 因此“函数大小相同”不是有效安全条件；任何重新链接的 UI 镜像即使保持 4MB
  头和段表，也必须视为高风险。当前唯一可接受的显示修改仍是文本白名单内的
  字节补丁，不能以该路线实现按钮。

### VSYNC + 模型依赖闭合候选（2026-08-29）

- 在独立远端目录 `/home/max/openvela-p4-workflow-vsync-integration-20260829`
  组合了 VSYNC 帧刷新实现、既有流程界面、`workflow_state` 状态机及完整
  TFLM/验证样本依赖。
- 完整 NuttX 链接成功；产物已保存为
  `artifacts/workflow-vsync-model-linked-20260829/nuttx-workflow-vsync-model-linked-20260829.bin`，
  大小 841,028 字节，SHA-256
  `cdf11fd69ec60a157376e4f38154dc83135612496b0407a8b6f9b01843e42f14`。
- 静态验收：`dsi_submit_status_ui_frame()` 仅执行 framebuffer cache-sync
  和 dirty 标记；VSYNC ISR 安装/卸载闭合；八个按钮文本和命中区存在；
  `workflow_state` 离线测试及命中区测试均 PASS；按钮回调已接入状态派发和
  前置条件拒绝路径。GT911 松开后由轮询主循环执行待处理工作：拍摄仅采集
  RGB565 帧，标签检测仅对已拍帧执行 TFLM，其余结果按钮只展示已有结果。
  界面已增加当前阶段的 `PROCESSING / PLEASE WAIT` 可见状态；结果、标签状态
  和忙状态更新均经 cache-sync + VSYNC dirty-frame 提交，UI 重启时会清空旧状态。
  该候选尚未刷写 COM12，
  `0xE0000` 模型容器未访问。
- 图库删除、逐图进度条、标注图渲染与位置偏差真值验证尚未接入，不能视为
  整机功能完成。
- 该候选仍不是物理验收版本。实机验证前必须完成 ELF/反汇编安全审计、
  明确单次回滚命令，并确认只写 `0x2000` 应用区。

- The isolated full workflow UI candidate built successfully as
  `dd3c28ecf38ee2e7312ec820e88c1039e0086f6bda7618859a5c5f45a9ebf336`
  (301,368 bytes), but it was rejected before any device write.
- Static review found that its `dsi_submit_status_ui_frame()` calls
  `dsi_gdma_stop()` followed by `dsi_gdma_start()` for each UI mutation.
  This is the previously observed black-screen/flicker risk path, so the
  candidate is not eligible for board validation.
- The isolated build tree was restored to the frozen DSI source and a clean,
  full rebuild exactly reproduced the accepted image:
  `22e48e28d361173c21882b1c9ee5e02402246563ec3b56ce810747878823e743`.
- No board flash was performed. The protected external model container at
  `0xE0000` was not accessed.

### DSI 动态刷新修复完成 (2026-08-27 10:55)
- **问题**: 运行中修改 framebuffer 后不能可靠显示到屏幕
- **根因**: dsi_submit_status_ui_frame() 每次都执行 DMA stop/start 循环
- **修复**: 改为 cache-sync only，DMA ISR 自动 re-arm
- **文件**: esp32p4_dsi.c (3处修改)
- **测试**: 全部通过（动态刷新/触摸反馈/按钮切换/DMA持续性）
- **SHA-256**: 793e471aa4f06413b7a61b5c8f97e72c681e1ffc87e168353648ff21c9960374
- **状态**: ✅ 完成

### DSI 动态刷新修复 + 按钮绑定 (2026-08-28)
- 完成 DSI 动态刷新修复（cache-sync only, ISR re-arm）
- 完成按钮功能绑定框架（workflow callback 机制）
- 修复触摸坐标 X 轴镜像
- 发现 DSI DMA→bridge 数据通路存在闪烁问题（详见交接文档20）
- 完整交接文档：docs/handoffs/20-complete-handoff-20260828.md
- 备份文件：.codex/camera_diag-energy-level/backup-20260827-1141/
- SHA-256: 793e471aa4f... (esp32p4_dsi.c MD5: 007219d5d0db07b7767b4f2b6a088d63)

## 归档保留式文字界面升级（2026-08-28）

- 已实机证明：当前环境重新构建的最小对照镜像也会黑屏；稳定归档与重建镜像的入口、RAM 段地址和段长度相同，但 RAM 段内容大范围不同。因此不再将当前重建产物用于板端显示测试。
- 新候选直接以已验收归档为底，只替换已定位界面文字区域 `0x10914` 至 `0x1099b` 内的八处固定长度字符串。入口、段表、代码、DMA、触摸逻辑、该文字区域外的所有字节和总大小均保持与归档一致。
- `nuttx-ui-textsafe-20260828.bin`：300,172 字节，SHA-256 `cf4b03b0d61d04617b86cff29e910060277025b6e0e4395f78cf53c32bb6cf14`；仅写入 `0x2000`，写入校验和独立 `verify-flash` 均通过，`0xE0000` 未改动。待用户确认亮屏及长期按下变深、松开恢复反馈。

### ESP-DL 手动探针接入复验（2026-09-01，进行中）

- 已恢复 `openvela-vm-jul` 的 SSH 连通性，并在独立目录
  `/home/max/openvela-p4-espdl-probe-20260901` 中工作；原集成树、稳定 UI 镜像和板卡 Flash 均未改动。
- 隔离副本仅增加手动命令 `camera_diag --espdl-probe`，其入口调用既有
  `m15_espdl_loader_probe()`；该命令不会由 UI、开机脚本或触摸流程自动触发。
- 复验确认：ESP-DL P4 专用汇编需要 Espressif 的
  `riscv32-esp-elf` 工具链；通用 xPack 工具链不支持 `xespv/xesploop`，不能用于该加速路径。
  已用临时 `/tmp/riscv32-unknown-elf-shim` 指向 Espressif 工具链，应用库正在完整重建，P4 汇编和
  ESP-DL C++ 源已开始通过编译。
- 下一验收门：完成 apps 与 NuttX 最终链接，收集未闭合的 ABI/FbsModel/硬件内核符号；在静态链接闭合之前，
  禁止生成板端候选，更禁止写入 `0x2000` 或 `0xB90000`。

#### 硬浮点全量构建闭合（2026-09-01）

- 已在完全隔离的远程副本 `/home/max/openvela-p4-espdl-probe-20260901` 完成一次从对象文件开始的全量硬浮点重建；没有修改本地稳定 UI 镜像、没有写入开发板，也没有访问模型分区 `0xE0000`。
- 最终链接和镜像生成均成功：`nuttx` SHA-256 为 `d7c13729fa4e471b311bf92c83e2c749b5e953cefcc3f2d72abecf583cda85d6`，`nuttx.bin` SHA-256 为 `1fa561670b85c785b965080dbdf8d58aa6689cc399bfc355ee5397c70dfdecca`。
- 最终 ELF 的 `readelf` 属性为 RISC-V 单精度硬浮点 ABI（含 `f`、`d` 及 ESP32-P4 向量/循环扩展）；显示 DSI HAL 对象也确认按含 `f` 的 ABI 重新生成，已排除旧软浮点对象残留。
- `nm -C nuttx` 已确认包含 `dl::Model::run(...)`、`dl::Model::load(...)` 等官方 ESP-DL 推理实现，且没有未解析的 `fbs::` 或 `dl::` 符号。至此，“ESP-DL 能否在硬浮点 OpenVela 候选中构建并链接”结论为通过。
- 风险边界不变：这不是稳定 UI 的可替代镜像。它改变了整个系统 ABI，尚未经过上板启动、显示、触摸、相机及模型推理的隔离验证；在完成独立启动指纹、回滚方案和逐项硬件验收前，禁止刷写。

#### ESP-DL 候选的 Flash 容量结论（2026-09-01）

- 当前 Simple Boot 布局中，应用只可安全写入 `0x2000..0xDFFFF`，共 909,312 字节；`0xE0000` 起始处是受保护的现有 TFLite 模型容器。
- 首个硬浮点 ESP-DL 候选为 1,710,100 字节。按 `0x2000` 写入会延伸到 `0x1A3813`，覆盖模型区 800,788 字节，已明确拒绝，未刷写。
- 随后在第二个隔离副本中移除了旧 TFLite Micro 执行器和三张 256×256 RGB565 离线验证样本，并确认最终 ELF 仍包含 `dl::Model::run(...)`、不再含 `tflite::MicroInterpreter`。候选降至 1,251,768 字节（SHA-256 `99c32ef0b34a7c6e019141a58cfe23f7c8f115e437ea1560ccc92c1156b1d8a5`），但仍超出安全应用区 342,456 字节，末地址为 `0x1339B7`，结论仍为 **不可刷写**。
- 这说明容量瓶颈不是离线样本或 TFLite Micro，而是官方 ESP-DL 执行库和其 C++ 运行时的最低接入成本。官方 `libfbs_model.a` 在该版本中以单一预编译对象提供，无法在不取得官方可裁剪执行器源代码的情况下，仅保留 YOLO 所需算子来回收这 342KB。
- 后续只有两条诚实可行的路线：①取得可裁剪、与当前模型算子集匹配的 ESP-DL 官方执行器源/库；②重新规划 Flash 布局，将现有模型容器安全迁移到已审计的空闲区域并同步更新加载逻辑。两条路线都属于新的高风险迁移任务，必须先建立完整备份与回滚程序并获得单独授权；当前稳定 UI、触摸、相机流程与 `0xE0000` 模型保持冻结。

#### 强制归档与 ABI 审计结果（2026-09-01）

- 强制重建隔离 `apps/libapps.a` 已完成，并消除了此前混入的原工程旧对象：archive 中现在只保留
  `openvela-p4-espdl-probe-20260901` 路径的 `camera_diag_main` 与 `m15_espdl_probe` 对象；
  `--espdl-probe` 命令字符串已在 archive 中可检出。这证明手动探针入口不再被旧对象遮蔽。
- 随后的 NuttX 最终链接给出可复现的两层 ABI 结论。第一层：NuttX 配置声明使用工具链 C++ 运行库，
  但实际生成的 `staging/libxx.a` 是 8 字节空 archive，最终链接也没有带入 `libstdc++.a` / `libsupc++.a`。
  在隔离环境中显式加入与 ESP-DL 相同 Espressif 工具链的这两个库后，原先的
  `std::__cxx11`、`std::__throw_length_error` 等 C++ 未定义符号消失。
- 第二层为当前硬阻塞：官方 ESP-DL 3.3.9 的 P4 `libfbs_model.a` 使用单精度硬浮点 ABI，
  而当前 OpenVela/NuttX 目标使用软浮点 ABI。链接器明确拒绝二者混合：
  `can't link single-float modules with soft-float modules`。该库同时承载 `fbs::FbsModel` 的实现；
  官方组件与同版本 Git 标签 `v3.3.9` 的完整文件树均只提供该预编译库，不提供可用的
  `FbsModel` 实现源码，因此不能通过重新编译一两个 C++ 文件解决。
- 结论：ESP-DL 算子源和 P4 汇编可以在当前 OpenVela 配置中编译，但官方封闭 FBS 执行器与现有软浮点
  OpenVela ABI 不兼容。安全的后续路线必须是：取得与软浮点 ABI 兼容的官方 FBS 库/源码，或者在独立、
  可回滚的系统级实验中评估把整个 OpenVela 目标迁移到硬浮点 ABI；后者会整体重链接，不能用于当前稳定 UI。
- 本轮没有写入板卡、没有生成可刷写候选，也没有访问 `0x2000`、`0xE0000` 或 `0xB90000`。当前稳定 UI、
  触摸、相机流程保持冻结。隔离构建日志保存在
  `/home/max/openvela-p4-espdl-probe-20260901/evidence/`。

#### ABI 路线交叉核验（2026-09-01）

- 先前独立 ESP-IDF 加速证明 `energy_label_p4_inference/build/mipi_isp_dsi.elf` 的 RISC-V 属性包含
  `f` 浮点扩展以及 ESP P4 向量/循环扩展；工具链提供的对应多库为 `ilp32f`。这与官方
  `libfbs_model.a` 的单精度浮点属性一致。
- 当前 OpenVela 配置明确为 `CONFIG_ARCH_FPU` 未启用，`CONFIG_ARCH_DPFPU` 未启用，应用 archive
  的目标属性也不含 `f`。所以“独立 ESP-IDF 能运行 ESP-DL”与“当前软浮点 OpenVela 不能链接该执行器”
  并不矛盾。
- 若继续加速路线，下一技术阶段应是全隔离的硬浮点 OpenVela 配置可构建性审计（工具链、NuttX 架构、
  ESP HAL、C++ 运行库和启动 ABI 一并重建）。该阶段不会、也不能替换现有稳定 UI 镜像；只有先完成
  静态构建、启动指纹、明确回滚方案后，才可能提出一次单独的板端实验。

#### ESP-DL 候选上板前备份与映射修正（2026-09-01）

- 板卡已只读确认：ESP32-P4 v3.2，16MB Flash，MAC `e8:f6:0a:e3:a9:5c`。
  未执行任何 Flash 写入。
- 完成候选覆盖范围的可恢复备份：`0x002000..0x133fff`，共 1,253,376 字节；
  SHA-256 为 `fdf5de8d371f45c0b335c29794413d6f53acea3fa728a8197a278a6fd0161b0c`。
  备份通过 64KB 分段只读取得；一次 USB 短包在 `0x112000` 自动重试后成功，随后新鲜读取的 4KB
  样本与备份逐字节一致。
- 恢复包位于 VM：
  `/home/max/openvela-p4-espdl-trim-20260901/artifacts/flash-backup-pre-espdl-migration-20260901/`。
  `recover_espdl_candidate.py` 默认拒绝写入，只有显式传入 `--execute` 且备份尺寸与 SHA-256 均完全
  匹配时才会写回该范围。
- 候选 `m15_espdl_probe` 已由错误的物理 Flash 指针改为 `spi_flash_mmap` 映射；会检查数据 MMU
  可用页数与 `OVM15ED` v2 容器清单。模型实际 payload 长度已纠正为 `0x321cd0`，FBS 头偏移为 16 字节。
- 修正后候选镜像：`nuttx.bin` 1,252,008 字节，SHA-256
  `0c0309a7f4b86956760490626ea66cf27a437f8f28b450ff5ed44ec49168ca07`；ELF 含硬浮点 `f` ABI、
  `spi_flash_mmap` 和 `dl::Model::run(...)`。
- 重要：候选按 `0x2000` 写入会覆盖旧 TFLite 容器的开头约 342,696 字节（从 `0xE0000` 起）。
  虽已有回滚备份，仍属于实际破坏性迁移；未经用户对本次覆盖的明确确认，不得刷写。

#### ESP-DL 硬浮点候选实机结论与回滚（2026-09-01）

- 经用户明确授权后，候选镜像 `0c0309a7f4b86956760490626ea66cf27a437f8f28b450ff5ed44ec49168ca07`
  曾写入 `0x2000`，烧录器的写入哈希校验通过，随后按相同长度只读回读并逐字节匹配；因此候选写入
  本身无传输错误。
- 实机结果：屏幕黑屏。候选 USB 控制台无有效启动输出，故不能以串口继续定位显示链路；但稳定 UI
  的显示启动是本项目优先约束，结论为此“硬浮点全系统 + ESP-DL”候选**不具备上板资格**。
- 已立即执行受哈希保护的恢复脚本，完整写回 `0x2000..0x133fff` 备份范围；烧录器校验通过，用户已
  确认稳定 UI 恢复正常。恢复备份在本机与 VM 中均保留，SHA-256 均为
  `fdf5de8d371f45c0b335c29794413d6f53acea3fa728a8197a278a6fd0161b0c`。
- 后续不得再次刷写该候选或同类“全量硬浮点重链接”镜像。模型性能优化应回到稳定软浮点 OpenVela
  基线：优化现有 TFLite 模型输入尺寸、线程/内存布局、量化模型与后处理，而不是替换整个显示系统 ABI。

#### 现有模型缩小输入的性能/精度筛选（2026-09-01）

- 为避免凭经验缩小模型输入，已在完整 285 张验证集上只读验证项目十类 `models/best.pt`；
  结果保存为 `evidence/project-best-downscale-benchmark-20260901.json`。基准 256px 的 mAP50 为
  0.8511；直接用同一权重缩小到 192/160/128px 后，mAP50 分别为 0.5290/0.2825/0.1862。
- 虽然 192/160/128px 的卷积计算理论上分别约减少为 256px 的 56.25%/39.06%/25%，但由于该模型
  原训练输入为 640px，直接缩放会严重破坏小缺陷与等级文字的特征。此路线不具备部署资格，禁止把
  这些“同权重低分辨率”版本转换或写入板卡。
- 性能根因复核：当前 TFLM `Register_CONV_2D_INT8()` 在非 CMSIS-NN、非 Xtensa 目标会直接退化为
  `Register_CONV_2D()` 参考实现；P4 的稳定软浮点 OpenVela 树没有可直接启用的 ESP-NN TFLM 后端。
  因此预处理、NMS 和 UI 优化都无法把实测约 112 秒的卷积主耗时降到可接受水平。
- 可行后续：以 160px 或 192px 为部署目标重新训练（不是仅缩放现有权重），加入小污渍/褶皱的
  专项增强与知识蒸馏，并在量化后先做全验证集精度筛选；或在不替换 OpenVela 启动 ABI 的前提下，
  设计可隔离的 ESP-DL/ESP-NN 推理协处理边界。前者低风险但预期仍是数十秒级，后者有望到数秒级
  但属于较大的系统集成工作。

#### 30 秒目标：192 像素极小模型重新训练（2026-09-01，进行中）

- 已建立专用十类检测模型 `models/yolov8t_10class.yaml`：887,830 参数、2.7 GFLOPs；当前
  256px 部署模型为约 3,007,598 参数、8.1 GFLOPs。它是重新训练的独立架构，不是将旧权重直接缩图。
- 已完成一次 CPU 单轮校准，证据为 `evidence/yolov8t-192-cpu-calibration-20260901.json`：训练+验证
  总耗时 155.37 秒，验证 mAP50 为 10.71%（从零训练一轮，不能作为质量结果）。
- 已在本机启动有边界的第一阶段：20 轮、192px、CPU、十类完整训练集；训练脚本为
  `tools/train_yolov8t_192_stage1.py`。按校准值预计约 50 分钟，仅产生本地权重，不转换、不刷写、
  不访问板卡和现有稳定 UI。
- 性能假设待验证：按 2.7/8.1 的模型计算比例及 192/256 的像素比例，参考卷积的理论目标约为当前
  112 秒的 18.7 秒；加入量化、预处理和后处理后，目标是整次结果小于 30 秒。该数字尚不是板端承诺，
  必须在重新训练、INT8 转换和真实板端计时均通过后才可确认。
- 下一验收：20 轮结束后先在全部 285 张验证集评估 `best.pt`；只有精度趋势可接受时，才继续训练并
  执行 ESP 等效 INT8 筛选。当前稳定固件、显示、触摸、相机和 `0xE0000` 模型容器继续冻结。

##### 第一阶段结果与第二阶段（2026-09-01，进行中）

- 第一阶段 20 轮已完成：总耗时 1,711.58 秒；`stage1-20e/weights/best.pt` 的峰值 mAP50 为
  62.75%，最后一轮为 62.10%。这已超过将旧模型直接缩到 192px 的 52.90%，证实“在目标尺寸重新
  训练”有效，但仍低于原 256px 85.11%，不能部署。
- 最后一次逐类验证显示小类短板明显：破损约 0.87% mAP50、褶皱约 18.13%、外箱约 0.45%；能效
  等级和标签类较高。故不能只看总 mAP50，也不能据此宣称达到项目检出要求。
- 已从第一阶段最佳权重启动本地第二阶段 30 轮继续训练，脚本
  `tools/train_yolov8t_192_stage2.py`。输入尺寸和网络结构不变，因此它只提高精度、不改变 30 秒
  延迟目标的理论估算；不转换、不刷写、不访问板卡。

##### 192 像素极小模型第二阶段独立精度结论（2026-09-01）

- 第二阶段 30 轮已完成，最佳权重为
  `artifacts/yolov8t-192-30s-20260901/stage2-30e/weights/best.pt`；训练过程中的最高 mAP50 为
  73.596%（第 29 轮）。不得用最后一轮权重替代自动保存的 `best.pt`。
- 已在完整 285 张验证集上独立复测 FP32 `best.pt`，证据为
  `evidence/yolov8t-192-stage2-fp32-baseline-20260901.json`：精确率 77.91%、召回率 72.44%、
  mAP50 73.12%、mAP50-95 50.04%。这是量化前、主机 FP32 的质量基线，不是板端速度结论。
- 十类 mAP50 分别为：等级 1 44.49%、等级 2 52.17%、等级 3 56.00%、等级 4 61.82%、等级 5
  54.67%、污渍 98.99%、破损 2.59%、褶皱 28.69%、标签 99.37%、外箱 1.60%。破损、褶皱、外箱
  三类明显不符合项目缺陷检测质量要求。
- 结论：本候选虽然相对“原模型直接缩至 192px”的 52.90% mAP50 有提升，但在 FP32 阶段就已严重
  丢失关键缺陷类别；因此停止其 INT8 转换和板端刷写，不允许把精度问题误判为量化后果。稳定 UI、触摸、
  相机固件和 `0xE0000` 模型容器继续冻结。
- 后续优化应转向提高 192px 专用模型的小类别学习能力（针对破损/褶皱/外箱的数据核验、定向增强、类别
  重采样或蒸馏），每轮先做完整逐类 FP32 验证；只有关键类恢复到可接受范围后，才进入 INT8 与 ESP 等效筛选。

##### 192 像素候选第三阶段续训（2026-09-01，进行中）

- 已从第二阶段的 `best.pt` 启动第三阶段 100 轮本地 CPU 续训，脚本为
  `tools/train_yolov8t_192_stage3.py`。保持 192px 输入和 2.7 GFLOPs 架构不变，故不会抬高原定的
  板端延迟理论目标；本阶段只尝试让现有特征继续收敛，尤其改善破损、褶皱、外箱。
- 训练只写入本地 `artifacts/yolov8t-192-30s-20260901/stage3-100e/`，不导出 INT8、不连接或刷写板卡，
  不修改稳定 UI、触摸、相机固件，也不访问 `0xE0000`。
- 完成标准：训练结束后必须用独立逐类 FP32 脚本重新验证；若关键类仍低，则本候选停止进入量化阶段并转为
  数据/增强与架构路线评估，不能以总 mAP 掩盖缺陷类漏检。

##### GPU 续训与 30 秒候选阶段结论（2026-09-01）

- 已确认本机 CUDA 环境为 `C:\Users\ASUS\miniconda3\python.exe`、PyTorch `2.4.1+cu121`、
  NVIDIA GeForce RTX 4060 Laptop GPU。训练环境已对齐到 Ultralytics 8.4.62，并在 GPU 上复验了第二阶段
  起点 mAP50=73.12%，与 CPU 独立基线一致后才开始续训。
- GPU 低学习率续训 60 轮已完成，最佳权重：
  `artifacts/yolov8t-192-30s-20260901/stage4-gpu-continuation-60e/weights/best.pt`。完整 285 张验证集
  的独立 GPU FP32 报告为 `evidence/yolov8t-192-stage4-gpu-fp32-baseline-20260901.json`：精确率
  80.44%、召回率 73.44%、mAP50 74.37%、mAP50-95 51.04%。
- 与第二阶段相比，总 mAP50 仅从 73.12% 增至 74.37%；褶皱从 28.69% 增至 32.79%，但破损仅从 2.59%
  到 2.67%，外箱仅从 1.60% 到 2.47%。关键缺陷类并未恢复，普通续训已呈明显边际收益递减。
- 决策：停止此 192px 极小模型的常规续训、INT8 转换和板端刷写。其结果可作为“30 秒计算规模可行但当前
  数据/模型质量不可部署”的证据；严禁以总体 mAP50 掩盖破损和外箱的实际漏检。
- 后续低风险路径应先审计验证集的破损/外箱标注与成像尺度，再做定向的数据增强、类别采样或蒸馏实验；每个
  候选必须先通过逐类 FP32 门槛才可进入量化。稳定 UI、触摸、相机与现有模型容器保持冻结。

##### 标签裁剪与四类缺陷专用实验（2026-09-01）

- 标注尺度审计证据：`evidence/defect-annotation-scale-audit-192px-20260901.json`。在全帧 192px
  输入中，破损 110 个实例中有 36 个最窄边小于 8px（其中 10 个小于 4px），外箱 83 个实例中有 24 个
  小于 8px；这是原 192px 小模型对这两类严重失真的直接几何原因。
- 已建立独立、不修改原数据的标签 ROI 裁剪集（训练 5,432 张、验证 285 张），并完成 10 类 192px GPU
  验证：`evidence/label-crop-yolov8t-192-fp32-baseline-20260901.json`。总 mAP50 79.63%，褶皱 36.17%、
  破损 4.65%、外箱 5.03%。裁剪可提升特征尺度，但十类共同训练仍压制破损/外箱。
- 已完成标签裁剪后的四类专用（污渍、破损、褶皱、外箱）192px GPU 候选，报告：
  `evidence/label-crop-defect4-yolov8t-192-fp32-baseline-20260901.json`。四类 mAP50：污渍 99.16%、
  破损 10.49%、褶皱 42.58%、外箱 8.95%。相对十类裁剪候选，破损与外箱明显提升，说明任务拆分有效。
- 结论：该候选仍不得进入量化或板端，因为破损/外箱尚低于原模型 256px 的 18.49%/14.56% 基线；但“先
  定位标签、再做缺陷专用识别”成为后续应优先验证的可行架构。下一步是评估更高分辨率标签裁剪在延迟预算内
  对破损/外箱的增益。稳定 UI、触摸、相机与模型 Flash 继续冻结。

##### 224 像素标签裁剪四类候选（2026-09-01）

- 已完成相同 2.7 GFLOPs 架构、224×224 输入的四类标签裁剪 GPU 实验。独立 FP32 结果见
  `evidence/label-crop-defect4-yolov8t-224-fp32-baseline-20260901.json`：污渍 99.28%、破损
  13.47%、褶皱 44.44%、外箱 10.27%，四类总 mAP50 60.26%。
- 相比 192×192 四类候选，224×224 使破损增加 2.98 个百分点、褶皱增加 1.87 个百分点、外箱增加
  1.32 个百分点，说明增加标签裁剪分辨率方向正确；但破损、外箱仍未达到原模型 256px 全帧的
  18.49%/14.56% 基线，不能宣称精度已还原。
- 速度边界：224²/192² = 1.361。按 192px 2.7-GFLOPs 候选约 18.7 秒卷积理论估计，224px 的卷积
  理论约 25.5 秒，连同裁剪和后处理可能贴近 30 秒上限；此数据尚未经过 TFLM 实测，禁止视为板端承诺。
- 决策：224px 是目前精度最高、仍可能满足 30 秒预算的离线候选，但尚不进入 INT8/板端。下一轮优先研究
  极小破损/外箱的专项训练策略（尺寸感知采样、定向增强或高分辨率二次细分），而不是继续普通轮次续训。

##### 原始模型与 30 秒候选的可比性说明（2026-09-01）

- 已直接读取原始 `E:\energy_label_defect_detection\P\models\best.pt` 的嵌入训练参数：原始模型为
  YOLOv8n，640×640、250 轮、batch 16、GPU 训练、采用预训练权重及完整数据增强；并非 192px 的
  小模型训练产物。
- 在相同 285 张验证集上，原始模型即使被降到 256px 评估，仍得到 mAP50 85.11%、精确率 88.01%、
  召回率 82.23%。这证明原始模型整体检测能力显著高于当前 192px 候选第一阶段的 mAP50 62.75%。
- 当前差距来自三个同时存在的有意速度换取条件：输入像素由 640/256 降到 192、网络由约 300.76 万
  参数/8.1 GFLOPs 降到 88.78 万参数/2.7 GFLOPs，以及训练轮次目前仅 20 轮对原始 250 轮。小目标
  的破损、褶皱、外箱最先受损；不是原始模型本身精度下降。
- 后续报告必须把“原始 640/256 模型”“原模型直接缩到 192”“192px 重新训练候选”分别列出，禁止把
  不同输入尺寸、模型规模或训练轮次的结果当作同一精度。

##### 224像素小目标尺寸加权实验（2026-09-02，已淘汰）

- 本轮只在本地 RTX 4060 Laptop GPU 上进行 FP32 离线实验；未导出 INT8、未连接或写入开发板，稳定 UI、触摸、相机流程和 `0xE0000` 模型容器均未改动。
- 新增可复用的尺寸分桶评估工具 `tools/evaluate_defect4_size_bins.py`。它以原始验证图坐标计算 IoU，再按映射至 224 像素输入后的最窄边分桶，避免将缩放坐标与原图坐标混用。原 224 候选的证据为 `evidence/label-crop-defect4-224-size-bin-recall-20260902.json`。
- 原 224 候选的关键事实：破损在小于8像素、8到12像素、大于12像素三个桶的召回率分别为 8.33%、46.15%、83.56%；外箱分别为 20.00%、33.33%、86.44%。这证明低精度主要集中于缩放后极小的破损和外箱，不是量化引起，也不是模型对全部尺度都失效。
- 已生成独立加权数据集 `artifacts/label-crop-defect4-small-object-weighted-20260902`：仅训练集把含小于12像素破损或外箱的样本额外复制三次，训练样本从 6,303 增至 10,676；验证集仍保持原始 285 张，未复制、未污染。生成脚本为 `tools/build_defect4_small_object_weighted_dataset.py`。
- 从原 224 候选权重继续训练的加权候选在第11轮前的最佳验证 mAP50 为 59.87%，低于原候选 60.26%，因此主动停止，不再消耗 GPU。其保留的 `best.pt` 仅作为淘汰证据，不得导出或上板。
- 加权候选的分桶证据为 `evidence/label-crop-defect4-224-small-weighted-size-bin-recall-20260902.json`：小外箱召回从 20.00% 提至 33.33%，但小破损从 8.33% 降至 4.17%，大目标也有下降。这种以重复采样换取局部提升的策略不能接受，已淘汰。
- 下一轮低风险方向：保持标签 ROI 与四类任务、保持 224 像素和 2.7 GFLOPs 架构，构建“包含小破损/外箱时先放大后随机裁剪”的定向尺度增强。它需要先在原始 285 张验证集上完成逐类 FP32 和尺寸分桶验证；只有总体指标与关键小目标同时改善，才能考虑 INT8 等效验证。任何板端写入仍须单独授权并说明地址、大小、模型区影响和回滚方法。

##### 当前模块进度（2026-09-02）

| 模块 | 进度 | 状态 |
| --- | ---: | --- |
| 稳定显示与触摸 | 98% | 已冻结，未经单独授权不修改 |
| 按钮与流程绑定 | 92% | 已完成主要交互流程 |
| 相机采集 | 88% | 双击预览/拍摄流程已验证 |
| 模型板端接入 | 92% | 稳定 TFLM 已接入；加速替代路线冻结 |
| 缺陷流程与结果标注 | 45% | 小目标精度优化进行中，尚不可验收 |
| 整机验收 | 66% | 等待可接受的模型精度与端到端验证 |

##### 224像素小目标放大裁剪实验（2026-09-02，已淘汰）

- 为避免“重复同一张小目标图”的副作用，新建了独立的定向尺度增强集 `artifacts/label-crop-defect4-small-object-zoom-20260902`。原训练清单保留 5,432 张图，并且只对未经过常规增强的基础图中每个小于12像素的破损/外箱实例生成一次以该实例为中心的 60% 视野裁剪，再放大回原图尺寸；共新增 328 张，训练总数 5,760。源数据和验证集均未改动。构建脚本：`tools/build_defect4_small_object_zoom_dataset.py`。
- 候选从原 224 权重开始，在 RTX 4060 Laptop GPU 上完成 30 轮 FP32 微调；训练脚本 `tools/train_label_crop_defect4_224_small_zoom_gpu.py`，权重 `artifacts/label-crop-defect4-yolov8t-224-small-zoom-20260902/train-30e/weights/best.pt`。本轮未导出 INT8、未访问开发板或任何 Flash 区。
- 对原始 285 张验证集独立复测的证据为 `evidence/label-crop-defect4-yolov8t-224-small-zoom-fp32-baseline-20260902.json`：总体 mAP50 60.01%、精确率 75.64%、召回率 58.84%；逐类 mAP50 为污渍 99.09%、破损 13.15%、褶皱 44.05%、外箱 10.32%。原 224 候选为总体 60.26%、破损 13.47%、褶皱 44.44%、外箱 10.27%。
- 尺寸分桶证据为 `evidence/label-crop-defect4-224-small-zoom-size-bin-recall-20260902.json`：小于8像素破损维持 8.33%，小于8像素外箱从 20.00% 到 26.67%，但大于12像素的破损/外箱召回降至 79.45%/81.36%。
- 决策：该候选未同时提高总体质量和关键小目标表现，已淘汰；不得 INT8 转换、不得部署或刷写。至此已证实“重复采样”和“固定中心放大裁剪”都不足以恢复极小目标，后续需要在数据质量/标注、可变尺度增强或更高分辨率二阶段细检中选择下一轮路线。

##### ESP-DL 硬浮点候选重新诊断（2026-09-02，进行中）

- 本轮仅访问隔离远程树和执行一次无写入复位；当前稳定 Flash 内容没有被读取、擦除或写入。远程虚拟机 `openvela-vm-jul` 的 `/dev/ttyACM0` 可用，隔离目录包括 `openvela-p4-espdl-hardfpu-20260901`、`openvela-p4-espdl-trim-20260901` 与稳定集成树。
- 复位反馈环测试：`esptool.py --chip esp32p4 --port /dev/ttyACM0 chip-id` 成功返回 `0`，随后以 115200 波特率从同一 ACM 端口采样 12 秒，得到 `0` 字节。稳定固件也不经该端口输出启动日志；既有捕获工具需要未透传给 VM 的 CH340 UART0 `/dev/ttyUSB0`。因此当前 ACM 端口不能作为“候选黑屏/正常启动”的自动判定通道。
- 静态布局复核：稳定集成镜像 `nuttx.bin` 为 841,028 字节；硬浮点 ESP-DL 精简候选 `nuttx.bin` 为 1,252,008 字节。若均从 `0x2000` 写入，后者必然越过受保护模型容器起点 `0xE0000`，且超出安全应用区约 342KB。该候选已因实机黑屏回滚，禁止再次刷写。
- 当前排名假设：一，硬浮点候选在早期 ABI/FPU 或启动初始化阶段失败；二，候选的完整系统链接布局破坏了稳定启动路径；三，候选可能早期启动但当前 VM 未透传实际控制台，导致无法获得区分性日志。现有证据已确认容量/布局风险，尚不能仅凭黑屏证明其中某一项是唯一根因。
- 下一验收前置条件：必须先恢复 CH340 UART0 至 VM（出现 `/dev/ttyUSB0`），或在用户单独授权后刷写带最小启动标记且具备完整回滚范围的临时候选。未满足此前置条件，不允许再次刷写硬浮点/ESP-DL 候选。

##### ESP-DL 启动指纹反馈环已建立（2026-09-02）

- 用户已将 CH340 UART0 透传到 VM，远程设备现为 `/dev/ttyUSB0`，下载/复位口保持为 `/dev/ttyACM0`。已用现有稳定 Flash 内容运行 `tools/capture_openvela_boot_uart.py`：`reset_exit=0`、`uart_bytes=2523`、`booting_nuttx=1`、`nsh=1`、`dsi_active=1`、`gt911=1`。
- 采集脚本现已收紧为真正的失败关闭门禁：复位失败或 NuttX、NSH、状态 UI、GT911 任一启动指纹缺失时返回非零并打印 `boot_signature=FAIL`；稳定基线实测返回 `boot_signature=PASS`。该命令现可作为历史黑屏候选的自动红绿反馈环。
- 历史硬浮点候选已重新只读核对：`/home/max/openvela-p4-espdl-trim-20260901/nuttx/nuttx.bin`，1,252,008 字节，SHA-256 `0c0309a7f4b86956760490626ea66cf27a437f8f28b450ff5ed44ec49168ca07`，入口 `0x4ff489f6`。它不是安全应用区内的候选，写入会覆盖模型容器。
- 本地可回滚备份已确认：`artifacts/flash-backup-pre-espdl-migration-20260901/recovery-range-0x002000-0x134000.bin`，覆盖 `0x2000..0x133fff` 共 1,253,376 字节。远程恢复脚本在显式执行前会验证该范围长度与固定 SHA-256。下一步若要复现红色启动指纹，必须先取得用户对“候选写入覆盖模型区、失败即完整回滚”的单独确认。

##### ESP-DL 硬浮点候选实机复现与恢复（2026-09-02，已完成）

- 在用户已确认“候选会覆盖模型区、失败即完整回滚”后，历史硬浮点候选被**仅此一次**写入 `0x2000`，长度 1,252,008 字节。esptool 写入校验通过；该次写入覆盖范围为 `0x2000..0x133fff`，因此暂时覆盖了 `0xE0000` 起的模型容器。
- UART0 自动启动指纹记录在 `evidence/espdl-hardfpu-candidate-boot-20260902.txt`。候选已到达 `*** Booting NuttX ***`，并完成 7 个映像段加载，但 12 秒内没有出现 `NuttShell (NSH)`、`persistent status UI active` 或 `GT911 product=911`；自动判定为 `boot_signature=FAIL`。这已排除“此前只是 VM 没有收到真实 UART0 日志”的解释。
- 随后立即使用已校验的回滚包恢复完整 `0x2000..0x133fff` 范围。恢复写入校验通过；复验日志 `evidence/espdl-hardfpu-recovery-boot-20260902.txt` 中 NuttX、NSH、状态 UI 与 GT911 四项指纹均存在，自动判定 `boot_signature=PASS`。稳定 UI、触摸、相机流程和原模型容器均已恢复。
- 当前可证实的故障窗口是“映像段加载完成之后、NSH 启动之前”。候选配置启用了 `CONFIG_ARCH_FPU=y`、硬浮点 ABI 及 ESP-DL 相关 C++ 运行时；尚不能仅凭现有日志将根因唯一归为 FPU、C++ 运行时或早期硬件初始化之一。后续如继续此路线，必须在隔离树中加入不改变 Flash 布局的早期阶段标记，并先通过静态/模拟检查；不得再次刷写本候选。
- 决策：ESP-DL 全系统硬浮点迁移路线冻结为“已复现启动阻断、不可部署”。保留自动 UART0 启动指纹脚本及两份实机日志作为回归证据；后续板端加速只可从不要求把现有 OpenVela 全系统改为硬浮点 ABI 的方案中重新立项。

##### 当前模块进度（2026-09-02，实机诊断后）

| 模块 | 进度 | 状态 |
| --- | ---: | --- |
| 稳定显示与触摸 | 98% | 已恢复并冻结 |
| 按钮与流程绑定 | 92% | 已完成主要交互流程 |
| 相机采集 | 88% | 双击预览/拍摄流程已验证 |
| 模型板端接入 | 92% | 稳定 TFLM 已接入；ESP-DL 硬浮点替代路线已冻结 |
| 缺陷流程与结果标注 | 45% | 小目标精度优化进行中，尚不可验收 |
| 整机验收 | 66% | 等待可接受的模型精度与端到端验证 |

##### ESP-DL 早期启动定位的离线续查（2026-09-02，已暂停）

- 对实机失败候选和稳定集成镜像的只读配置比较显示，候选的关键额外系统级选项为 `CONFIG_ARCH_FPU=y`；两者均从相同的 ESP32-P4 启动路径进入 `nx_start`。现有 UART0 证据把停止窗口限定在 ROM 完成 7 个映像段加载、打印 `*** Booting NuttX ***` 之后，到 `NuttShell (NSH)` 之前。
- 已核对启动路径：`esp_start.c` 在完成芯片修订检查、ROM 系统调用表、板级资源初始化后调用 `nx_start()`；ESP 平台启动层在此之前还会执行 C/C++ 静态构造与二级组件初始化。因此当前仍有三个待区分边界：硬浮点/FPU 早期运行时、C++ 静态构造/ESP-DL 组件初始化、`nx_start` 内的内存或硬件初始化。
- 曾尝试在**远程隔离树**中只启用已有启动进度标记并离线重建。该树的临时 `riscv32-unknown-elf-gcc` shim 已失效，且其历史 apps/Kconfig 链接存在递归，`olddefconfig` 在构建前即失败；未生成新镜像、未访问开发板 Flash。原 `.config` 和候选 `nuttx.bin` 均已恢复，候选散列仍为 `0c0309a7f4b86956760490626ea66cf27a437f8f28b450ff5ed44ec49168ca07`。
- 决策：不修补或重用这棵历史候选树来继续盲测。若未来重开 ESP-DL 路线，必须新建干净隔离树、固定完整工具链与 apps 来源，在不改变 Flash 布局的前提下先获得可复现的早期阶段日志；只有日志明确跨过此前失败边界且候选大小不覆盖 `0xE0000` 模型区，才可另行申请一次实机写入授权。

##### 最小硬浮点启动诊断候选（2026-09-02，待实机验证）

- 已在新的 VM 临时隔离树 `/tmp/openvela-p4-espdl-startupdiag-20260902` 完成可复现构建。该树固定 ESP RISC-V C/C++ 工具链，恢复了历史树被污染的 apps 索引，并只在副本中处理构建元数据；稳定树、历史候选和开发板 Flash 均未改动。
- 候选保留 `CONFIG_ARCH_FPU=y`、`CONFIG_DEBUG_FEATURES=y`、NuttX 启动路径、状态 UI、触摸与相机应用；反汇编确认 `__esp_start` 含 10 个 ROM 输出调用，其中包含进入 `nx_start()` 前的既有 `A/B/C/D` 阶段标记。它不包含 ESP-DL 汇编、C++ 源或 `libfbs_model.a`：`ASRCS = m13_validation_samples_stub.S`、`CXXSRCS =`。
- 产物为 `artifacts/espdl-min-hardfpu-startupdiag-20260902.bin`，315,056 字节，SHA-256 `191a74f28e86c7121965868de7dfd179e684163f7eae2937bbddde802577c572`。若从 `0x2000` 写入，结束地址为 `0x4EEAF`，完全位于安全应用区 `0x2000..0xDFFFF` 内，**不会访问或覆盖** `0xE0000` 起的模型容器。
- 诊断判据：若该最小硬浮点候选不能跨过 `A/B/C/D` 并到达 NSH，则根因位于硬浮点/FPU 或其与现有 NuttX 平台启动链的组合；若它能到达 NSH、UI 和 GT911，则可将历史失败进一步收敛到 ESP-DL/C++ 静态初始化或完整链接布局。该候选的实机验证与无条件回滚结果见下节。

##### 最小硬浮点启动诊断实机结果（2026-09-02，已完成）

- 已写入并验证 `artifacts/espdl-min-hardfpu-startupdiag-20260902.bin`：地址 `0x2000`，大小 `315,056` 字节，SHA-256 `191a74f28e86c7121965868de7dfd179e684163f7eae2937bbddde802577c572`。写入范围为 `0x2000..0x4EEAF`，未访问或覆盖 `0xE0000` 起的模型容器。
- UART0 自动诊断日志 `evidence/espdl-min-hardfpu-boot-20260902.txt` 显示 7 个映像段加载完毕并输出 `ABCD`，但 12 秒内没有 `NuttShell (NSH)`、`persistent status UI active` 或 `GT911 product=911`，判定为 `boot_signature=FAIL`。这说明失败发生在 `__esp_start` 进入 `nx_start()` 之后，而不在 ESP-DL 汇编、ESP-DL C++ 静态初始化、完整映像容量或模型区覆盖。
- 已无条件回滚 `artifacts/flash-backup-pre-espdl-migration-20260901/recovery-range-0x002000-0x134000.bin` 到 `0x2000..0x133FFF`，大小 `1,253,376` 字节，SHA-256 `fdf5de8d371f45c0b335c29794413d6f53acea3fa728a8197a278a6fd0161b0c`。恢复日志 `evidence/espdl-min-hardfpu-recovery-boot-20260902.txt` 已验证 NuttX、NSH、状态 UI 与 GT911 均存在，`boot_signature=PASS`。
- 决策：冻结 `CONFIG_ARCH_FPU=y` 的全系统硬浮点迁移路线，禁止重复刷写该候选或其等价变体。后续板端加速只考虑不改变现有 NuttX 全系统浮点 ABI 的方案；稳定显示、触摸、相机流程与模型容器继续冻结。

##### FPU 初始化补丁候选（2026-09-02，离线构建完成，待实机确认）

- 只读对比发现 ESP32-P4 的 `esp_start.c` 已包含 `riscv_internal.h`，但未调用通用 RISC-V 的 `riscv_fpuconfig()`；其它 RISC-V 板级启动路径均在早期调用该函数。该函数设置 `mstatus.FS` 并清零 FCSR，当前候选仅在 `showprogress("D")` 与 `nx_start()` 之间、且仅当 `CONFIG_ARCH_FPU=y` 时调用一次。
- 补丁源档：`artifacts/espdl-min-hardfpu-fpuinitdiag-20260902/esp_start.c`；构建副本：VM `/tmp/openvela-p4-fpuinitdiag-20260902`。构建成功，未修改稳定树、模型容器或开发板 Flash。
- 候选镜像：`artifacts/espdl-min-hardfpu-fpuinitdiag-20260902/nuttx.bin`，大小 `315,052` 字节，SHA-256 `df105a5977d03f917ab672cb61a5dd0dabac9a268315dd62d69eb23471179ca6`。拟写入 `0x2000..0x4EEAB`，不覆盖 `0xE0000` 起的模型容器；实机验证若失败，立即回滚 `recovery-range-0x002000-0x134000.bin` 至 `0x2000..0x133FFF` 并运行 UART 启动指纹复验。
- 静态验证：`riscv_fpuconfig`、`nx_start` 与 `__esp_start` 已链接到镜像；该补丁只检验“FPU 在进入首个调度器上下文前未初始化”的假设，不构成已修复结论。

##### FPU 初始化补丁候选实机结果（2026-09-02，已完成）

- 已按确认把 `artifacts/espdl-min-hardfpu-fpuinitdiag-20260902/nuttx.bin` 写入 `0x2000..0x4EEAB`；写入校验通过，模型容器 `0xE0000` 未被访问或覆盖。
- UART0 日志 `evidence/espdl-min-hardfpu-fpuinitdiag-boot-20260902.txt` 与无补丁最小硬浮点候选完全同类：7 个映像段加载完、输出 `ABCD`，但 12 秒内未出现 NSH、状态 UI 或 GT911，`boot_signature=FAIL`。因此“ESP32-P4 启动路径仅仅漏掉 `riscv_fpuconfig()`”被实机证伪，禁止重复刷写该补丁候选。
- 已立即回滚稳定包至 `0x2000..0x133FFF`；恢复日志 `evidence/espdl-min-hardfpu-fpuinitdiag-recovery-boot-20260902.txt` 验证 NuttX、NSH、状态 UI 与 GT911 均存在，`boot_signature=PASS`。
- 决策：所有 `CONFIG_ARCH_FPU=y` 的全系统启动候选均冻结。剩余高概率根因是 ESP32-P4 的 FPU 上下文/异常处理与 NuttX 调度器不兼容，或 ESP-IDF 已知 RISC-V FPU 限制；这不再是可通过单点启动补丁安全解决的问题。板端推理继续保留已稳定的软浮点 TFLM 路线，后续性能提升改从模型结构、输入分辨率和算子实现优化进行。

##### 稳定软浮点 TFLM 基线入口审计（2026-09-02，已完成）

- 新增只读工具 `tools/run_camera_diag_uart_command.py`：先打开 CH340 UART0，再由 USB 下载口触发复位；等待 NSH 后发送固定 `camera_diag` 命令，并要求指定日志标记出现。工具不含写 Flash、擦除或模型区访问逻辑。
- 实机运行 `camera_diag --tflm-validation-sample stain` 的结果归档为 `evidence/tflm-stain-baseline-20260902.txt`。启动、常驻 UI 与 GT911 均正常，但当前稳定固件返回 `camera_diag: unknown option '--tflm-validation-sample'`，所以本次未发生模型调用，也没有产生新的速度数据。
- 结论：当前板上已验收的 UI/触摸固件与 VM 中包含 TFLM 诊断入口的后续源码树存在构建版本漂移。现有恢复包的完整范围可稳定启动，但其镜像格式含隐藏 ROM 段，不能依据 `841,028` 字节的简单前缀与 VM 中同尺寸 `nuttx.bin` 等同。
- 后续前置条件：先重建可复现的“稳定 UI/触摸 + 已验证软浮点 TFLM 诊断入口”基线，并通过 UART 启动指纹；只有该基线与当前稳定交互逐项验收一致后，才允许开始真实板端性能计时。此阶段不修改模型容器，也不尝试 ESP-DL/硬浮点路线。

##### 当前板端可用状态与项目进度（2026-09-02，基线入口审计后）

| 模块 | 进度 | 当前状态 |
| --- | ---: | --- |
| 稳定显示与触摸 | 98% | 当前恢复镜像已通过 NuttX、NSH、常驻状态 UI、GT911 启动指纹。 |
| 按钮与流程绑定 | 55% | 离线状态机和历史候选已完成，但当前稳定镜像只保留状态 UI/触摸，不包含可验收的工作流按钮。 |
| 相机采集 | 80% | CSI/ISP 与实时预览曾通过实机验证；当前稳定镜像仍保留诊断入口，但未重新完成按钮流验收。 |
| 模型板端接入 | 70% | 模型容器与历史软浮点调用证据保留；当前稳定镜像未暴露 TFLM 诊断命令，无法进行当代性能复测。 |
| 缺陷流程与结果标注 | 45% | 离线模型优化继续进行；现阶段尚无与当前稳定 UI 同版本的端到端验收。 |
| 整机验收 | 60% | 启动、显示、触摸已稳定；按钮、相机、模型与结果链需在同一可复现镜像中重新闭环。 |

##### ESP-IDF ESP-DL 产品运行架构首版（2026-09-02，已构建，未刷写）

- 已在 `models/energy_label_yolov8_p4/espidf_p4_inference/main/product_runtime.*` 建立独立于硬件驱动的产品状态机：启动、待机、取景、帧已就绪、标签检查、缺陷检查、结果、故障；状态机同时保留 `已完成张数/总张数`，供处理等待页使用。
- 现有 ESP-IDF 相机、GT911、DSI、ESP-DL、YOLO 解码和 NMS 入口已接入状态迁移日志。此版本仍保留原有自动单帧诊断行为，尚未把 GT911 的触摸区域绑定为产品按钮，因此没有改变开发板或现有 OpenVela 固件。
- 在 VM 的隔离副本 `/home/max/energy_label_p4_product_runtime` 完整交叉编译通过：ESP-IDF `6.2.0`、ESP-DL `3.3.9`、目标 `esp32p4`。产物 `mipi_isp_dsi.bin` 为 `2,636,944` 字节，SHA-256 为 `005d55f98a984a94cbe89c3c32a8b0bbd51066727e85c9f191891ace508e7e94`，小于 `4 MiB` 应用分区限制，剩余约 37% 空间。
- 未写入任何 Flash 地址；当前 OpenVela 稳定镜像和其模型容器均未访问。下一项工作是把 GT911 的明确按钮区域与 `capture/gallery/label/inspect` 事件绑定，然后才申请一次独立的 ESP-IDF 全分区刷写和回滚授权。

| 模块 | 进度 | 当前状态 |
| --- | ---: | --- |
| 稳定显示与触摸 | 98% | OpenVela 稳定基线冻结；ESP-IDF 端仅有既有 GT911 反馈验证。 |
| 按钮与流程绑定 | 58% | 产品状态机已构建，真实按钮区域与动作尚未绑定。 |
| 相机采集 | 80% | ESP-IDF 已验证 SC2336 到 RGB565 单帧采集；产品取景流程待迁移。 |
| 模型板端接入 | 72% | ESP-DL 单帧相机推理已实机验证；产品状态机集成已构建，未刷写。 |
| 缺陷流程与结果标注 | 47% | YOLO 解码/NMS 和框叠加存在，缺陷判定及中文结果页待完成。 |
| 整机验收 | 60% | 尚未有 ESP-IDF 产品镜像的端到端实机验收。 |

##### ESP-IDF DSI 加相机传感器初始化探针刷写（2026-09-02，实屏通过）
- 已按用户单独授权从 VM 的 `/dev/ttyACM0` 刷写并校验三段：`0x2000` bootloader（23,616 字节）、`0x8000` 分区表（未变化，跳过写入）和 `0x10000` 应用（262,608 字节）。应用 SHA-256 为 `7f6059b3a037647f4952cedf7b9b315210adb78bd62c210584add5d0278ebdaf`；刷写后已执行硬复位。
- 本次未写入、擦除或校验 `0x410000` ESP-DL 模型区。若实屏黑屏、闪烁或持续重启，立即用 `artifacts/flash-backup-pre-espdl-migration-20260901/recovery-range-0x002000-0x134000.bin` 恢复 `0x2000..0x133FFF`，不继续增加相机集成内容。
- 本机 `COM12` 当前不存在；VM 中 `/dev/ttyACM0` 与 `/dev/ttyUSB0` 均存在。对 CH340 UART0 的 12 秒只读采样没有收到文本，不能用空日志判定初始化失败。用户已实屏确认静态 DSI 页面保持，且中央状态块变为黄色，故 SC2336 传感器初始化通过。

##### ESP-IDF DSI 加 CSI/ISP 初始化探针（2026-09-02，已构建，未刷写）
- 新建隔离工程 `models/energy_label_yolov8_p4_dsi_csi_isp_init_probe`，以已实屏通过的 DSI 加 SC2336 探针为基线，增加 CSI 控制器创建与启用、ISP RAW8 到 RGB565 配置与启用；明确不调用 `esp_cam_ctlr_start()`，不排队事务、不采集帧、不加载 ESP-DL 模型。
- VM 隔离副本 `/home/max/energy_label_p4_dsi_csi_isp_init_probe_20260902` 已完成 ESP-IDF 6.2.0 / ESP32-P4 交叉编译。应用 `build/mipi_isp_dsi.bin` 大小 285,968 字节，SHA-256 `567abb34fba5e806596eb78149f21da7b0e6630903f8434ac35486dc2566f785`；bootloader SHA-256 `5bad4ecca52fee8b938a4891ebf1529147c8fbca3f3e40fe5d416b134966d175`，分区表保持 `4c44584a8a05b648d1b7afa37704b67235912fd4510e92cc60501880df30bf58`。
- 候选刷写段仅为 `0x2000` bootloader（23,616 字节）、`0x8000` 分区表（3,072 字节）和 `0x10000` 应用；`0x410000` ESP-DL 模型区不在写入清单中。实屏成功信号为中央状态块由黄色变为橙色；黑屏、闪烁或重启则立即用既有恢复包写回 `0x2000..0x133FFF`。

##### ESP-IDF DSI 加 CSI/ISP 初始化探针实机失败与回滚（2026-09-03）
- 按用户授权写入三段探针后，屏幕持续黑屏；CH340 UART0 读到 CPU0 `main` 任务连续触发 task watchdog，判定 CSI/ISP 初始化阶段阻塞。上电瞬间短暂黑屏本身不构成故障，但本次持续黑屏与看门狗同时出现，属于失败。
- 已立即使用已核对的恢复包 `artifacts/flash-backup-pre-espdl-migration-20260901/recovery-range-0x002000-0x134000.bin`（1,253,376 字节，SHA-256 `fdf5de8d371f45c0b335c29794413d6f53acea3fa728a8197a278a6fd0161b0c`）恢复 `0x2000..0x133FFF`，写入校验通过并硬复位；`0x410000` ESP-DL 模型区未访问。
- 结论：CSI/ISP 直接初始化候选冻结，不再原样重刷。后续若继续相机链路，只能从已验证的单帧采集路径拆分更小增量，并在刷写前重新建立可观测启动标记。
- 用户已确认回滚后屏幕恢复亮起；OpenVela 稳定显示基线实屏恢复验收通过。

##### ESP-IDF DSI 加 CSI 仅创建探针（2026-09-03，已构建，未刷写）
- 为隔离前一候选的看门狗故障，新建 `models/energy_label_yolov8_p4_dsi_csi_create_probe`。该候选只保留已通过的 DSI 与 SC2336 初始化，然后调用 `esp_cam_new_csi_ctlr()`；不启用 CSI、不注册回调、不分配或排队帧、不启动采集、不初始化 ISP、不加载模型。
- VM 隔离副本 `/home/max/energy_label_p4_dsi_csi_create_probe_20260903` 构建通过。应用 `build/mipi_isp_dsi.bin` 大小 274,352 字节，SHA-256 `80ee1c941b7645eee297c8f75df6390907b3c12e2617c16b23a65083389d9fea`；沿用 23,616 字节 bootloader 与 3,072 字节分区表。候选未执行任何 Flash 写入。
- 若获授权，刷写段仅为 `0x2000`、`0x8000`、`0x10000`，不触及 `0x410000` 模型区；实屏只需确认页面保持和中央状态块进入 CSI-create 标志色。失败仍使用既有恢复包回滚。
- 用户已确认该探针实屏页面持续显示且中央状态块保持黄色；DSI、SC2336 初始化和 CSI 控制器创建通过，未出现黑屏或看门狗。故障边界进一步缩小到 CSI enable 或 ISP 配置。

##### ESP-IDF DSI 加 CSI 创建并启用探针（2026-09-03，已构建，未刷写）
- 在仅创建探针基础上只增加 `esp_cam_ctlr_enable()`；不注册回调、不排队帧、不启动采集、不初始化 ISP、不加载模型。
- VM 隔离构建通过，应用 `mipi_isp_dsi.bin` 大小 274,592 字节，SHA-256 `6931d6fbd3a24be158fbd22b2689ff8eb63a49607477a10d3b657773028e04fd`。候选仍未写入开发板，`0x410000` 模型区不在清单中。

##### ESP-IDF DSI 加 CSI 启用与 ISP 仅创建探针（2026-09-03，已构建，未刷写）
- 在已实屏通过的 CSI 启用候选上只增加 `esp_isp_new_processor()`，配置 CSI RAW8 到 RGB565；不启用 ISP、不注册采集回调、不排队或启动帧、不加载模型。
- VM 隔离构建通过，应用 `mipi_isp_dsi.bin` 大小 285,552 字节，SHA-256 `21b83e5f94053b5223ba011ee3c2691a02aaad77ac65af390e4acc622bb6f5b2`。未执行 Flash 写入，`0x410000` 模型区不在清单中。
- 用户已确认该探针实屏页面持续显示且中央状态块保持黄色；ISP 处理器创建通过，故障边界缩小到 ISP enable 或后续采集事务。

##### ESP-IDF DSI 加 CSI 启用与 ISP 创建并启用探针（2026-09-03，已构建，未刷写）
- 在 ISP 仅创建候选上只增加 `esp_isp_enable()`；不分配帧、不注册采集回调、不排队或启动采集、不加载模型。
- VM 隔离构建通过，应用 `mipi_isp_dsi.bin` 大小 285,840 字节，SHA-256 `480cb655c2760c1fe917af45ad5dcfc725825d11b4645c94525a110dc90ac86f`。未执行 Flash 写入，`0x410000` 模型区不在清单中。

##### ISP 启用探针实屏通过（2026-09-03）
- 用户已确认页面持续显示且中央状态块保持黄色；CSI 创建、CSI 启用、ISP 创建和 ISP 启用均通过，未出现黑屏或看门狗。
- 下一故障边界为帧缓冲分配、采集回调/事务注册或 `esp_cam_ctlr_start()`，后续按此顺序继续拆分。

##### ESP-IDF 采集前配置探针（2026-09-03，已构建，未刷写）
- 在已通过 CSI/ISP 启用基础上增加 DMA-capable RGB565 帧缓冲、CSI 回调和事务注册；明确不调用 `esp_cam_ctlr_start()`，不采集、不加载模型。
- VM 隔离构建通过，应用 `mipi_isp_dsi.bin` 大小 286,320 字节，SHA-256 `d0603e2ab01fdca522caa838aafd521a20ffe9e8d8d8da4321f9ec8425f72fb4`。未执行 Flash 写入，`0x410000` 模型区不在清单中。

##### 采集前配置探针首次实机失败与顺序修正（2026-09-03）
- 首次刷写版本在 CSI 已启用后才注册回调，UART0 明确报 `ESP_ERR_INVALID_STATE`：`driver starts already, not allow cbs register`，随后 `ESP_ERROR_CHECK` 主动 abort 并重启；失败原因已确定为调用顺序错误，不是帧缓冲分配失败。
- 已立即回滚 OpenVela，恢复包写入 `0x2000..0x133FFF` 并校验通过，`0x410000` 模型区未访问；用户应以屏幕确认恢复亮起。
- 修正版改为“先 `esp_cam_ctlr_register_event_callbacks()`，再 `esp_cam_ctlr_enable()`”，仍不调用 `esp_cam_ctlr_start()`。VM 隔离构建通过，应用 286,320 字节，SHA-256 `8d91c1519474ab738bc518662dd47035b59c0b6e424467f403b30c6a5f890896`；尚未刷写。
- 用户已确认修正版页面持续显示、中央状态块保持黄色；帧缓冲分配、回调注册和 CSI 启用顺序通过，故障边界缩小到 `esp_cam_ctlr_start()` 及后续等待帧。

##### ESP-IDF 仅启动采集探针（2026-09-03，已构建，未刷写）
- 在采集前配置修正版基础上只增加一次 `esp_cam_ctlr_start()`；不等待帧、不停止控制器、不做图像处理或模型推理。
- VM 隔离构建通过，应用 `mipi_isp_dsi.bin` 大小 286,640 字节，SHA-256 `895e71a444d7d76d68135813d5a302b5e112c775cf0c2fa8f03f37d0937393a1`。未执行 Flash 写入，`0x410000` 模型区不在清单中。

##### ESP-IDF 仅启动采集探针刷写（2026-09-03，待实屏确认）

- 用户已单独授权。刷写前只读确认 VM 连通、`/dev/ttyACM0` 可用，设备为 ESP32-P4 v3.2（MAC `e8:f6:0a:e3:a9:5c`）；构建产物大小、SHA-256 与上述记录一致。
- 已写入并逐段取得 `Hash of data verified`：`0x2000` bootloader（23,616 字节），`0x8000` 分区表（3,072 字节），`0x10000` 仅启动采集探针应用（286,640 字节）；随后已自动硬件复位。
- 本次写入最大至 `0x55FFF`，未访问、擦除或改写 `0x410000` ESP-DL 模型区。两个串口的读取窗口均未收到日志，当前以实屏“页面持续显示、无黑屏、无重启”作为通过依据。若失败，立即用 `artifacts/flash-backup-pre-espdl-migration-20260901/recovery-range-0x002000-0x134000.bin` 恢复 `0x2000..0x133FFF`，不触及模型区。

##### ESP-IDF 等待首帧探针（2026-09-03，已构建，待刷写授权）

- 基于“仅启动采集”实屏通过版，仅增加首帧回调等待：启动采集后最多等待 3 秒，成功打印 `first frame callback passed`，超时打印 `first frame timeout`；不停止控制器，不读取模型，不做预处理。
- VM 工程 `/home/max/energy_label_p4_dsi_capture_wait_frame_probe_20260903` 已完成编译；应用大小 286,960 字节，SHA-256 `bcc963adbd759a0f439acb4bb2ab253e20dcf67bfe19dac24ae4ff1b6fa5eb8d`。尚未刷写，仅待用户单独授权后写入 `0x2000/0x8000/0x10000`；`0x410000` 模型区不在清单中。

##### ESP-IDF 等待首帧探针刷写（2026-09-03，待实屏验收）

- 用户单独授权后，只读确认 `/dev/ttyACM0` 为 ESP32-P4 v3.2，随后将三段写入 `0x2000` bootloader、`0x8000` 分区表、`0x10000` 应用，三段均返回 `Hash of data verified`。应用最高写入地址约为 `0x560EF`，未触及 `0x410000` ESP-DL 模型区，已执行硬件复位。
- 串口读取窗口未收到文本输出，当前等待用户实屏确认：页面是否持续显示、中央状态块是否保持黄色，以及是否出现黑屏或重启。若失败，立即回滚现有 `recovery-range-0x002000-0x134000.bin`。

##### ESP-IDF 单帧采集并停止探针（2026-09-03，已构建，待刷写授权）

- 基于已通过的首帧回调等待版，在回调成功后调用 `esp_cam_ctlr_stop()`，对 RGB565 帧缓冲做缓存同步并计算整帧 FNV-1a 校验值，然后保持页面常亮。不加载模型，不进行推理。
- VM 工程已重新构建通过，应用大小 287,392 字节，SHA-256 `7059d2bd7f84c5b74c20a1059107f6b9f3d0d87d5a498e297f2a7982be08490c`。尚未刷写，待用户单独授权后仅写入 `0x2000/0x8000/0x10000`，不触及 `0x410000` 模型区。

##### ESP-IDF 单帧采集并停止探针刷写（2026-09-03，待实屏验收）

- 用户已单独授权，已将三段写入 `0x2000` bootloader、`0x8000` 分区表、`0x10000` 应用，均返回 `Hash of data verified`；应用最高写入地址约 `0x5629F`，未触及 `0x410000` 模型区，已执行硬件复位。
- 待用户实屏确认页面持续显示。通过后方可进入产品应用的真实采集动作；若黑屏或重启，立即回滚现有 `recovery-range-0x002000-0x134000.bin`。

##### ESP-IDF 产品应用采集工作流编译复核（2026-09-03）

- 已复核产品应用的 `CAPTURE` 工作流：从触摸动作入队，调用 `esp_cam_ctlr_start()`、等待首帧、`esp_cam_ctlr_stop()`，同步并保留 RGB565 帧缓冲，再返回 `FRAME READY`页。回调注册顺序保持为先注册、后 enable。
- VM 工程 `/home/max/energy_label_p4_product_runtime` 已完成整机重新构建，产物 `build/mipi_isp_dsi.bin` 大小 2,640,304 字节，小于 4 MiB 应用分区限制。该产物的创建清单包含 `0x410000` ESP-DL 模型分区，尚未刷写。
- 下一步是在实板上验收点击 `CAPTURE`：需要用户单独授权完整四段刷写（含模型区）；失败时使用现有 OpenVela 回滚包。

##### ESP-IDF 产品采集候选刷写（2026-09-03，待实屏验收）

- 用户已单独授权。刷写前只读确认产物与设备信息：ESP32-P4 v3.2，MAC `e8:f6:0a:e3:a9:5c`，应用 2,640,304 字节，模型 3,284,192 字节。
- 已完整写入并逐段取得 `Hash of data verified`：`0x2000` bootloader、`0x8000` 分区表、`0x10000` 产品应用、`0x410000` ESP-DL 模型；模型区写入终点约为 `0x731FFF`，已执行硬件复位。
- 串口读取未获取文本日志，待用户实屏验收 `CAPTURE`：应出现采集中页面，随后回到 `FRAME READY`。若黑屏、重启或无法触摸，立即停止应用验收并用回滚包恢复 OpenVela。

##### ESP-IDF 产品候选启动短暂蓝屏观察（2026-09-03）

- 用户反馈“先出现短暂蓝屏，随后恢复正常”。由于产品应用启动时会重新初始化 DSI，并在应用启动前短暂关闭背光，该现象更像启动过渡而非崩溃；随后能回到正常页面表明应用未持续重启。
- 当前不把该现象记为失败，但尚需实测 `CAPTURE`。下一步请点击一次 `CAPTURE`，验收采集中页面、首帧等待和 `FRAME READY` 回显。

##### ESP-IDF 产品 `CAPTURE` 实屏验收通过（2026-09-03）

- 用户确认产品候选启动后，`CAPTURE` 按钮可正常调度：显示采集中页面，完成首帧采集后返回 `FRAME READY`，页面未出现持续黑屏或重启。
- 该结果将相机采集进度更新为 97%，按钮与流程绑定更新为 80%，整机验收更新为 76%。下一步验收 `LABEL` 按钮与板端 ESP-DL 推理回显。

##### ESP-IDF 产品 `LABEL` 实机验收（2026-09-03，待用户点击）

- 当前已有 `FRAME READY` 页面和可用帧缓冲，`LABEL` 按钮已连接到板端 ESP-DL 预处理、推理、YOLO 解码与标签判定。
- 待实机点击验收：应先显示“正在运行”，推理完成后回到结果页并报告标签是否存在；若无反应、黑屏或重启，暂停后续推理验收。

##### ESP-IDF 产品界面文案与 LABEL 任务稳定性修正（2026-09-03，已构建，待刷写）

- 根据实屏反馈，明确定位原程序使用英文五点阵字体，直接传入 UTF-8 中文会产生乱码；同时增大产品动作任务栈，避免 LABEL/INSPECT 深调用时无响应。
- 已将产品应用中动作任务栈由 4,096 增至 16,384，触摸任务增至 6,144；调整处理页面的文案起始位，避免长文字被裁切。
- 产品工程已在 VM 重新构建通过，应用为 235,536 字节，SHA-256 `0397bfe6181b4348e3397ed836bf63566007fe96d88d038fc0a91a9617486a68`。本次尚未刷写；刷写会包含现有模型分区，需用户另行授权。

**修正：上述 235,536 字节候选与完整产品工程不一致，已作废，未刷写。当前开发板仍保留最近一次通过 `CAPTURE` 的完整产品镜像。之后将先恢复完整模型构建，再单独修正任务栈与中文字体，新候选未经授权不刷写。**

##### 产品工程完整性恢复与任务栈修正（2026-09-03）

- 已从本地保存的完整推理源码恢复 VM 产品工程，确认模型加载、YOLO 解码、按钮事件和相机采集代码均在。
- 仅将 `product_action` 任务栈调整为 16,384，`gt911_overlay` 任务栈调整为 6,144，用于排除 LABEL 推理时无响应；产品应用重新编译通过，应用大小 2,640,320 字节，SHA-256 `6f1433cf25c7f5168ec23e033b5626c8ac5cfbb12b7ef841904b47fee2817c83`。
- 这一版尚未刷写，也未加入真正中文字体；文案中文化需在字库资源定位后单独完成，避免再次引入 UTF-8 乱码。

##### 产品界面修正当前状态（2026-09-03）

- 已恢复并验证完整产品源码，仅增大动作任务栈后编译通过，产物体积与完整模型版一致，尚未刷写。
- 中文界面尚未刷入：现有五点阵字体不支持 UTF-8，直接改为中文会产生图中所示乱码。后续将使用最小中文字形表，只包含界面所需汉字，并对标题、状态、按钮逐行重新布局。

##### 产品字体与中文化实施边界（2026-09-03）

- 已确认当前固件只有 5×7 ASCII 点阵，不支持中文字符；直接传入 UTF-8 会将每个字节当做 ASCII 绘制，因而出现乱码和换行错位。
- 中文化的技术方案已定为“只嵌入界面实际用到的少量汉字点阵，提供 UTF-8 解码和宽度自动换行”，不引入整套 CJK 字库。该部分尚未编译入产品固件，避免再次刷入乱码版本。

| 模块 | 进度 | 当前状态 |
| --- | ---: | --- |
| 稳定显示与触摸 | 98% | 最小 ESP-IDF DSI 与 SC2336 初始化均已实屏通过；触摸未纳入当前探针。 |
| 按钮与流程绑定 | 74% | 已有设计与隔离实现，等待稳定相机显示链路回归。 |
| 相机采集 | 88% | 传感器初始化已实屏通过；CSI/ISP 初始化候选已构建，待实屏验证。 |
| 模型板端接入 | 75% | ESP-DL 单帧性能证据保留；当前探针刻意不加载模型。 |
| 缺陷流程与结果标注 | 50% | 等相机链路稳定后继续集成。 |
| 整机验收 | 66% | 已跨过 DSI 和传感器初始化；待 CSI/ISP 初始化及后续增量验收。 |

##### ESP-IDF 最小 DSI 启动探针（2026-09-02，已构建，未刷写）
- 新建隔离工程 `models/energy_label_yolov8_p4_dsi_boot_probe`，入口只完成 DSI 资源分配、面板复位/初始化、静态 RGB565 帧提交和常驻循环；不初始化相机、GT911、ESP-DL 或产品状态机。
- 该探针移除了 ESP-DL、传感器和模型分区写入配置，ESP-IDF 6.2.0 / ESP32-P4 单线程交叉编译通过。应用为 `219,056` 字节，SHA-256 为 `6d3f775c43f89db9ccebc988edc4455024793cf071aa0d296ab7003ec9888af1`。
- 生成的写入清单只有：`0x2000` bootloader（23,616 字节）、`0x8000` partition table（3,072 字节）、`0x10000` 静态 DSI 应用（219,056 字节）；明确不含 `0x410000`，不会写入 ESP-DL 模型分区。
- 当前板卡仍是用户已确认亮起的 OpenVela 恢复基线。下一验收必须单独授权：仅刷写上述三段，实屏应持续显示蓝底、青色标题条、绿色和白色状态块；失败时立即按既有恢复包写回 `0x2000..0x133FFF`。

| 模块 | 进度 | 当前状态 |
| --- | ---: | --- |
| 稳定显示与触摸 | 98% | OpenVela 稳定基线已恢复；ESP-IDF 最小 DSI 探针已构建，待首次实屏验证。 |
| 按钮与流程绑定 | 74% | ESP-IDF 设计已构建，等待最小显示先通过。 |
| 相机采集 | 83% | 既有单帧与预览证据保留，未纳入本次探针。 |
| 模型板端接入 | 75% | ESP-DL 路线有约 6.35 秒模型运行证据；本次探针刻意隔离模型。 |
| 缺陷流程与结果标注 | 50% | 待稳定显示与完整产品集成后回归。 |
| 整机验收 | 62% | 首先等待最小 DSI 启动实屏结果。 |

##### ESP-IDF 最小 DSI 启动探针刷写（2026-09-02，等待实屏验收）
- 已在用户单次授权后通过 `/dev/ttyACM0` 确认 ESP32-P4 v3.2（MAC `e8:f6:0a:e3:a9:5c`），并完成最小 DSI 探针写入：`0x2000` bootloader、`0x8000` partition table、`0x10000` 应用。三段均返回 `Hash of data verified`，随后已执行硬件复位。
- 实际应用写入范围为 `0x10000..0x457af`，未写入或擦除 `0x410000` ESP-DL 模型分区。当前实屏验收信号为蓝色背景、顶部青色横条和中央绿白状态块能持续显示。
- 若用户报告黑屏、闪烁或重启，立即停止后续 ESP-IDF 集成，并使用恢复包 `recovery-range-0x002000-0x134000.bin` 写回 `0x2000..0x133FFF`；若显示稳定，下一步才开始以相机初始化作为单独增量继续定位完整候选黑屏原因。

##### ESP-IDF 最小 DSI 启动探针实屏通过（2026-09-02）
- 用户确认静态页面可持续显示，顶部青色条和中央绿色/白色状态块均正确；因此 ESP-IDF 的最小 DSI 初始化、帧缓冲提交与常驻任务路径已通过实屏验收。
- 背景色未呈现为预期蓝色，其余色块正确。这是 RGB565 色值/面板色彩观感校准项，不是黑屏或显示启动故障；在单独做色彩表校准前，不据此更改已通过的最小启动路径。
- 结论：完整 ESP-IDF 候选的黑屏根因已缩小到后续集成阶段，优先排查相机/ISP 或其与 DSI 的资源初始化顺序；下一次候选必须以“最小 DSI 已通过”的版本为基线逐项增加资源。

##### ESP-IDF DSI 加相机传感器初始化探针（2026-09-02，已构建，未刷写）
- 新建隔离工程 `models/energy_label_yolov8_p4_dsi_camera_init_probe`。执行顺序固定为：DSI 资源分配和静态首帧提交 -> 打开背光 -> MIPI LDO 通道 3（2.5V）-> SC2336 SCCB/传感器初始化。未加入 CSI、ISP、帧采集、GT911、ESP-DL 或模型分区写入。
- 实屏信号设计：启动后先呈现已通过的 DSI 状态页；传感器初始化成功才将中央状态块改为黄色。若仍为初始绿白状态、黑屏或重启，则故障边界被缩至 LDO/传感器初始化阶段。
- ESP-IDF 6.2.0、ESP32-P4 单线程交叉编译通过。应用为 `262,608` 字节，SHA-256 为 `7f6059b3a037647f4952cedf7b9b315210adb78bd62c210584add5d0278ebdaf`；烧录清单为 `0x2000` bootloader、`0x8000` partition table、`0x10000` 应用，明确不含 `0x410000`。
- 当前板卡仍运行最小 DSI 实屏通过版本。下一步必须获得单独授权，若刷写失败则立刻用恢复包写回 `0x2000..0x133FFF`。

##### ESP-IDF 产品动作工作线程（2026-09-02，已构建，未刷写）

- `CAPTURE` 现在会在独立动作任务中显示静态运行页、重新启动 CSI 采集一帧、停止采集、保存 RGB565 帧并进入“帧已就绪”。`GALLERY` 复用保存帧显示；在当前已确认的相机与显示分辨率相同时直接显示 RGB565 帧。
- `LABEL` 与 `INSPECT` 在同一后台任务中保持“正在运行”页面，执行 ESP-DL 预处理与 `model.run()`。标签检查根据类别 `label` 判定并返回帧已就绪；缺陷检查执行既有 YOLO 解码/NMS，并重绘带框结果页。
- `dl::Model`、相机事务、相机帧缓冲区和执行上下文均改为长期有效对象，避免 `app_main()` 返回后触摸任务访问无效内存；DSI 面板和帧缓冲区只初始化一次，后续页面重绘复用同一资源。
- VM 完整交叉编译通过，产物 `mipi_isp_dsi.bin` 为 `2,640,304` 字节（`0x2849B0`），余约 37% 应用分区空间；未执行任何 Flash 写入。实机首次验证必须以 ESP-IDF 全分区刷写和 OpenVela 可恢复备份为前提。

| 模块 | 进度 | 当前状态 |
| --- | ---: | --- |
| 稳定显示与触摸 | 98% | OpenVela 稳定基线冻结；ESP-IDF 端动作隔离已构建，尚未实机验证。 |
| 按钮与流程绑定 | 74% | 四按钮已连接动作工作线程；待验证点击、页面切换及多次操作稳定性。 |
| 相机采集 | 83% | 单帧采集已实测；现已接入产品拍摄工作线程，待实机回归。 |
| 模型板端接入 | 75% | ESP-DL 推理已接入标签/缺陷按钮路径，待实机端到端复测。 |
| 缺陷流程与结果标注 | 50% | 解码/NMS、结果框与标签检查动作已接入；缺陷归类和中文结果说明待完成。 |
| 整机验收 | 62% | ESP-IDF 产品候选已构建，未刷写，尚无端到端实机验收。 |

##### ESP-IDF 产品候选刷写状态（2026-09-02，硬件下载模式待恢复）

- 已按用户单次授权从 VM 的 `/dev/ttyACM0` 识别到 ESP32-P4 v3.2（USB-Serial/JTAG，MAC `e8:f6:0a:e3:a9:5c`），并开始全分区 ESP-IDF 刷写。
- 首个刷写会话明确记录 `0x2000` 引导程序、`0x8000` 分区表和 `0x10000` 应用均已写入且各自 `Hash of data verified`。模型写入 `0x410000` 时远程会话日志在进度中断，未取得结束码，**不得将模型段视为已验收**。
- 后续只读下载探测和同一内容的后台重刷均停在 `Connecting...`；`/dev/ttyACM0` 设备仍存在但不再完成 ROM 下载握手。未执行 Flash 擦除命令，也未执行回滚，因为下载协议未建立时写入或回滚均不可能完成。
- 恢复前置：用户需将开发板手动置入下载模式（按住 `BOOT`，短按并松开 `RST`，再松开 `BOOT`），然后保持 USB 连接并回复“下载模式好了”。收到后先只读 `chip-id`，成功后重新完整写入四段并逐段验收；若写入或启动失败，立即使用现有 OpenVela 恢复包写回 `0x2000–0x133FFF`。

##### ESP-IDF 产品候选刷写完成（2026-09-02，待实屏验收）

- 用户已重新将板卡置入下载模式；只读 `chip-id` 成功，确认 ESP32-P4 v3.2、USB-Serial/JTAG 和 MAC `e8:f6:0a:e3:a9:5c`。
- 已从 VM 后台任务完整写入并分别验证四段：`0x2000` 引导程序、`0x8000` 分区表、`0x10000` 产品应用、`0x410000` ESP-DL 模型。刷写日志 `product_flash_attempt2_20260902.log` 对四段均记录 `Hash of data verified`，随后执行硬复位。
- 当前硬件验收状态：等待确认屏幕已启动并显示产品结果页；USB-Serial/JTAG 在硬复位后尚未自动提供新的启动文本，故不能用空日志代替实屏验收。若黑屏、重启或无触摸，按现有恢复包将 OpenVela 写回 `0x2000–0x133FFF`。

##### ESP-IDF 产品候选实屏失败与 OpenVela 回滚（2026-09-02）

- 实屏结果：ESP-IDF 产品候选黑屏。通过 CH340 的 24 秒启动窗口未取得任何应用输出，故候选不通过显示启动验收；禁止在未先建立可观测启动信号前重复刷写相同候选。
- 已立即使用恢复包 `recovery-range-0x002000-0x134000.bin`（SHA-256 `fdf5de8d371f45c0b335c29794413d6f53acea3fa728a8197a278a6fd0161b0c`）写回 `0x2000–0x133FFF`。写入日志确认 `Hash of data verified`，随后执行硬复位。
- 回滚范围不含 `0x410000`，该 ESP-DL 模型分区未被擦除或改写；恢复后的 OpenVela 启动文字仍未能从当前两个 USB 串口自动采集，故最终恢复验收必须以实屏为准。

##### ESP-IDF 产品候选第三次刷写（2026-09-02，已写入，待实屏）

- 用户更换 USB 接口后，VM 再次确认 CH340 与 Espressif USB JTAG/Serial 同时透传；只读 `chip-id` 正常识别 ESP32-P4 v3.2。
- 已第三次完整写入 ESP-IDF 产品候选：`0x2000`、`0x8000`、`0x10000`、`0x410000` 四段均在 `product_flash_attempt3_20260902.log` 中记录 `Hash of data verified`，随后硬复位。
- 当前等待实屏确认。本次仅改变 USB 物理接口和透传状态，不改变候选源码、应用二进制或模型内容；若仍黑屏，应把问题归到候选启动/显示初始化而非前一轮 USB 枚举失败。

##### ESP-IDF 候选第三次实屏失败后的回滚（2026-09-02）

- 新 USB 接口下的第三次 ESP-IDF 产品候选仍为黑屏，确认该问题与先前“无法识别 USB 设备”无关。当前 ESP-IDF 产品候选冻结，后续只能先建立最小 ESP-IDF 显示启动证据再重新集成产品应用。
- OpenVela 恢复包已再次写入 `0x2000–0x133FFF`，写入后 `Hash of data verified`，并已硬复位；`0x410000` 未访问。

##### OpenVela 恢复实屏验收（2026-09-02）

- 用户已确认回滚后的 OpenVela 屏幕“恢复亮起”。因此当前稳定显示基线恢复成功；ESP-IDF 产品候选保持冻结，不得在没有最小 ESP-IDF DSI 启动实屏证据的情况下再次刷写。
- 当前开发板有效状态：OpenVela 恢复镜像位于 `0x2000–0x133FFF`；`0x410000` 的 ESP-DL 模型残留不会被该 OpenVela 启动布局使用，也未影响本次恢复验收。

##### ESP-IDF 触摸动作队列（2026-09-02，已构建，未刷写）

- GT911 轮询任务现仅执行坐标换算、按压变深反馈和按钮动作投递；新的 `product_action` FreeRTOS 任务独占读取动作队列并调用产品状态机。此边界避免后续相机或 ESP-DL 长任务阻塞触摸 I2C 轮询。
- 二次增量编译通过，产物 `mipi_isp_dsi.bin` 为 `2,638,336` 字节（`0x284200`），小于 `4 MiB` 应用分区限制，剩余约 37%。未执行任何 Flash 写入。
- 仍未接入的工作线程：`CAPTURE` 重新启动相机并保留新帧，`GALLERY` 显示已保留帧，`LABEL` 与 `INSPECT` 调度预处理、ESP-DL、解码和结果页重绘。

| 模块 | 进度 | 当前状态 |
| --- | ---: | --- |
| 稳定显示与触摸 | 98% | OpenVela 稳定基线冻结；ESP-IDF 端已有 GT911 反馈和独立动作队列，未实机刷写。 |
| 按钮与流程绑定 | 64% | 四按钮坐标映射、状态迁移和异步动作队列已构建；具体动作工作线程待接入。 |
| 相机采集 | 80% | ESP-IDF 已验证 SC2336 到 RGB565 单帧采集；产品取景流程待迁移。 |
| 模型板端接入 | 72% | ESP-DL 单帧相机推理已实机验证；产品状态机集成已构建，未刷写。 |
| 缺陷流程与结果标注 | 47% | YOLO 解码/NMS 和框叠加存在，缺陷判定及中文结果页待完成。 |
| 整机验收 | 60% | 尚未有 ESP-IDF 产品镜像的端到端实机验收。 |

##### ESP-IDF 按钮事件绑定（2026-09-02，已构建，未刷写）

- 产品结果页已绘制 `CAPTURE`、`GALLERY`、`LABEL`、`INSPECT` 四个固定触摸区域。GT911 事件会按已校正的显示坐标映射为产品动作，再经过状态机校验；UART 会输出按钮名、是否接受和迁移后的状态。
- 为避免“结果页上按钮点击必然无效”，状态机新增“回看已拍帧”迁移：在结果页点击 `LABEL` 或 `INSPECT` 会先回到 `frame_ready`，再合法进入相应检查态。
- 已在 VM 隔离副本二次完整交叉编译通过。新产物大小 `2,637,856` 字节（`0x284020`），小于 `4 MiB` 分区上限，剩余约 37%；未执行 `flash`，未访问开发板 Flash。
- 未完成边界：四个动作的后台工作线程尚未连接，因此此阶段只验证“按钮可被识别并进入正确状态”，不宣称已可再次取景、显示图库或重新运行推理。

| 模块 | 进度 | 当前状态 |
| --- | ---: | --- |
| 稳定显示与触摸 | 98% | OpenVela 稳定基线冻结；ESP-IDF 端只有既有 GT911 反馈验证。 |
| 按钮与流程绑定 | 62% | 四按钮区域、触摸坐标映射和状态机事件已构建；动作工作线程待接入。 |
| 相机采集 | 80% | ESP-IDF 已验证 SC2336 到 RGB565 单帧采集；产品取景流程待迁移。 |
| 模型板端接入 | 72% | ESP-DL 单帧相机推理已实机验证；产品状态机集成已构建，未刷写。 |
| 缺陷流程与结果标注 | 47% | YOLO 解码/NMS 和框叠加存在，缺陷判定及中文结果页待完成。 |
| 整机验收 | 60% | 尚未有 ESP-IDF 产品镜像的端到端实机验收。 |
##### 中文界面候选编译（2026-09-03）
- 已生成 `tools/ui_glyphs.hpp` 中文 16×16 点阵字库，并上传至 VM 产品工程 `main/ui_glyphs.hpp`。
- 已接入 UTF-8 解码与中文绘制函数，按钮/状态/结果文案改为中文，保留 YOLOV8 INT8 等硬件/模型标识。
- VM `idf.py build` 成功；应用 `mipi_isp_dsi.bin` 大小 2,642,912 字节，SHA-256 `2043b7c6abfe6e9a0a78b00f385ce8c504bf765abb574982f1e389bc24480be1`。
- 本候选尚未刷写，当前硬件继续保持已验证的产品采集版本；刷写前必须单独授权。
##### 中文界面与 LABEL 修正版刷写尝试（2026-09-03）
- 已获用户明确授权，准备写入 `0x2000`、`0x8000`、`0x10000` 及模型区 `0x410000`。
- VM 执行 esptool 时停在 `Connecting...`，未写入任何分区，未改变当前固件。
- 需要用户将开发板重新置于下载模式后再继续：按住 BOOT，短按并松开 RST，松开 BOOT，保持 USB 连接。
##### 中文界面与 LABEL 修正版刷写完成（2026-09-03）
- 用户确认下载模式后，已成功写入四段：`0x2000` 引导程序、`0x8000` 分区表、`0x10000` 中文应用、`0x410000` ESP-DL 模型。
- 四段 `verify-flash` 均报告 `Verification successful (digest matched)`，芯片为 ESP32-P4 v3.2，MAC `e8:f6:0a:e3:a9:5c`。
- 已执行硬复位；等待用户确认屏幕中文界面是否正常，再测试 `拍摄`、`标签`、`缺陷`。
##### 中文排版与按钮可用性修正版（2026-09-03）
- 根据实屏照片确认中文字号过大、行距拥挤；已缩小中文绘制倍率并调整状态区/按钮文字位置。
- 未采集照片时仅“拍摄”保持启用，其余按钮显示禁用色；采集后“图库、标签、缺陷”启用，避免误认为按钮失效。
- 新应用已在 VM 编译通过，大小 `0x285400`（2,642,944 字节），SHA-256 `018fb7f7425108840cc7316a23864c5a6aa8a9b17bf1257aa1e16c607e637731`。
- 尚未刷写；需要用户对本次新候选再次授权。
##### 中文排版与按钮修正版刷写完成（2026-09-03）
- USB 设备恢复：`/dev/ttyACM0` 与 `/dev/ttyUSB0` 均已出现。
- 已按授权刷写引导程序、分区表、中文应用和 ESP-DL 模型；四段 `verify-flash` 均 `digest matched`。
- 应用写入大小 `0x285400`（2,642,944 字节），刷写后已硬复位，等待实屏确认排版及按钮。
##### 拍摄持续采集加速候选（2026-09-03）
- 已确认旧实现每次“拍摄”均启动 CSI 采集、等待首帧、停止采集，首帧等待上限为 5 秒；这不是模型推理时间。
- 新候选在首次启动并取得初始帧后保持相机采集，后续拍摄仅排队取得新帧，不再重复启动/停止相机。
- VM 编译通过：应用大小 2,642,784 字节，SHA-256 `c7f74ca67d647b98089763e3e7905ffbe3f0cf02a117b80bd205fc3d45dbdea5`。
- 未刷写；需实机确认连续采集与 DSI/触摸并行稳定性后才可更新进度。
##### 拍摄加速候选刷写完成（2026-09-03）
- USB 下载口恢复后已按授权写入四段固件与模型。
- `0x2000`、`0x8000`、`0x10000`、`0x410000` 均验证成功（`digest matched`），并已硬复位。
- 当前等待实机确认：拍摄是否明显变快、连续拍摄是否稳定、触摸和中文排版是否保持正常。
##### 实时取景双击拍摄候选（2026-09-03）
- 已加入取景状态逻辑：首次点击“拍摄”进入实时取景；取景期间持续采集并刷新当前帧；再次点击“拍摄”锁定当前帧并返回界面。
- 已保持相机运行，避免重复启停；构建通过，应用大小 `0x2854e0`（2,642,144 字节），SHA-256 `2162dda12572b2e3d4b4287cadd9643f0f34fb758ff1b0bb39e04ab6e63859dc`。
- 尚未刷写，需用户单独授权；刷写后重点验证实时画面是否连续、第二次点击是否锁帧。
##### 实时取景双击拍摄候选刷写完成（2026-09-03）
- 用户授权后已写入四段固件与模型，四段 `verify-flash` 均 `digest matched`。
- 应用大小 `0x2854e0`（2,643,168 字节），已执行硬复位。
- 等待实机验证：第一次点击“拍摄”是否保持连续画面，第二次点击是否锁定并返回；同时观察触摸反馈和中文排版。
##### 实时取景分辨率适配修正版（2026-09-03）
- 已定位上一版无实时画面的直接原因：取景页仅在相机分辨率等于屏幕分辨率时复制帧，实际分辨率不同时会回退到静态等待页。
- 已加入 RGB565 最近邻缩放，将相机帧填充到屏幕，并绘制取景框与提示文字；取景循环持续刷新，第二次点击拍摄锁定当前帧。
- VM 编译通过，应用大小 `0x285470`（2,642,032 字节），SHA-256 `5b76e7f059c605dcc9bebb8b817cfc16edfeeed5c7d2d5b1cd6d611dd4b7d3bc`。
- 尚未刷写；需用户授权 `同意刷写实时取景分辨率修正版`。
##### 实时取景分辨率适配修正版刷写完成（2026-09-03）
- 已按用户授权写入四段，`verify-flash` 对 `0x2000`、`0x8000`、`0x10000`、`0x410000` 均返回 `digest matched`。
- 已硬复位；待实机确认取景画面是否持续变化、第二次“拍摄”是否锁帧。
##### CSI 连续取帧修正版（2026-09-03）
- 已定位上一版实时画面仍不动的原因：`on_get_new_trans` 回调返回 `false`，CSI 只提供一次事务，后续没有持续帧。
- 已改为回调持续返回下一帧，并将 CSI 队列深度调整为2；VM 编译通过，应用大小 `0x285470`，SHA-256 `dc6a017c226618101421d1fcdfd6efa683c532aa87cde97cbed414df8bfb17d6`。
- 尚未刷写，需用户授权 `同意刷写 CSI 连续取帧修正版`。
##### CSI 连续取帧修正版刷写完成（2026-09-03）
- 已按授权刷写四段固件与模型，四段 `verify-flash` 均 `digest matched`，并已硬复位。
- 等待实机确认第一次点击“拍摄”后画面是否持续变化；若仍无画面，将转向独立相机显示任务方案。
##### 官方 CSI 持续接收与图库返回候选（2026-09-03）
- 已将实时取景 CSI 路径恢复为官方示例范式：`start` 一次、每帧循环调用 `receive`、`on_get_new_trans` 仅填充事务并返回 `false`，队列深度为1。
- CSI 输出格式与官方示例统一为 `CAM_CTLR_COLOR_RGB565`，与 ISP/显示帧缓冲路径保持一致。
- 图库改为再次点击“图库”返回主界面；编译通过，应用大小 2,643,136 字节，SHA-256 `a4b59746e53dc88eefef8cb7b82865dc82a9d58d0a8647e24a25d4fae318ba5d`。
- 尚未刷写，需单独授权；实机验收聚焦实时画面、图库返回与拍摄锁帧。
##### 官方 CSI 持续取景候选刷写完成（2026-09-03）
- 已按授权写入引导程序、分区表、应用和模型，四段验证均 `digest matched`，并已硬复位。
- 等待实机确认：第一次“拍摄”是否出现连续画面；第二次“拍摄”是否锁帧；“图库”再次点击是否返回。
##### 黑屏与 USB 透传诊断（2026-09-03）
- 用户反馈刷写 CSI 连续取帧候选后黑屏；VM 内 `/dev/ttyACM0`、`/dev/ttyUSB0` 同时消失。
- 宿主机仍识别 `USB-SERIAL CH340 (COM12)`，但 COM12 在8秒监听内无启动日志；暂不能确认应用是否进入串口输出阶段。
- 当前判断：需要 VMware 手动将 CH340 与 Espressif USB JTAG 设备连接到虚拟机；连接后先抓日志并优先回滚到已验证亮屏版本，暂停继续刷写取景候选。
##### 中文稳定拍摄基线确认（2026-09-05）
- 用户确认此前中文界面版本的“拍摄”功能体验良好，应作为产品稳定基线保留。
- 后续实时取景开发必须与该基线隔离；未经实机验证不得覆盖稳定拍摄路径。
- 当前黑屏候选暂停使用，下一步先恢复中文稳定拍摄基线，再独立验证实时取景。
##### 稳定中文版拍摄基线 + 推理加速合并候选（2026-09-05）
- 已停用导致黑屏的实时取景/持续 CSI 实验路径，恢复稳定中文版的单帧拍摄：点击拍摄后采集一帧、返回结果界面。
- 保留当前 ESP-DL `split_fast_int8_p4` 模型和推理侧现有优化；未改变模型输入 320×320、解码/NMS 逻辑，避免未经实测宣称更快。
- VM 编译通过；应用大小 `0x2853b0`（2,642,864 字节），SHA-256 `ce6acc2c5fcd583680cd12b6e440bac76f0f2aec5d0f7beb5851cfda4ea2522d`。
- 尚未刷写；下一步应先确认 USB 下载口后申请 `同意刷写稳定中文版拍摄+推理加速候选`，刷后实测拍摄和单张推理总耗时。

##### 稳定中文版拍摄 + 推理加速候选刷写完成（2026-09-05）
- 已获用户单独授权，并通过 VM 的 `/dev/ttyACM0` 识别 ESP32-P4 v3.2（MAC `e8:f6:0a:e3:a9:5c`）。
- 已写入四段：`0x2000` bootloader、`0x8000` 分区表、`0x10000` 稳定中文版单帧拍摄+推理加速应用、`0x410000` ESP-DL 模型。
- 写入过程及随后逐段 `verify-flash` 均返回 `Hash of data verified` / `Verification successful (digest matched)`；已执行硬复位。
- 应用 SHA-256：`ce6acc2c5fcd583680cd12b6e440bac76f0f2aec5d0f7beb5851cfda4ea2522d`；模型 SHA-256：`1d17062ef89726a487027a6eb23ee3d93237d57cf83a35cb57e7ebb030b7bc9b`。
- 当前仍待用户实屏确认：中文界面是否持续显示、触摸是否有效、单帧拍摄是否稳定、拍摄耗时及单张推理总耗时；未确认前不将整机验收标记为完成。
- 用户随后反馈刷写后黑屏，故该候选未通过启动验收；已停止后续测试。
- 已使用恢复包 `recovery-range-0x002000-0x134000.bin` 回滚 `0x2000..0x133FFF`，写入与独立校验均通过；`0x410000` 模型区未改动。
- 当前等待用户确认 OpenVela 稳定基线是否恢复亮屏；在确认前不再刷写新候选。

##### 黑屏根因定位与启动点亮候选（2026-09-05）
- 对比确认：黑屏候选在 `app_main()` 中先等待 5 秒、加载 ESP-DL、初始化相机/CSI/ISP、采集并推理，最后才初始化 DSI；因此启动及长时间运算期间屏幕没有可见输出，任何早期失败也会表现为黑屏。
- 已在源码中加入一次性 DSI 初始化与“正在启动/正在加载模型”等待页面，并让最终结果页复用同一 DSI/帧缓冲，避免重复初始化。
- 修正版已在 VM 编译通过，应用大小 `0x2854B0`（约 2,642,096 字节），尚未刷写；需单独实屏授权后再验证。
- 已按用户授权刷写引导程序、分区表和启动点亮修正版应用；三段写入及独立校验均返回 `Verification successful (digest matched)`，模型区 `0x410000` 保持未写入，已硬复位。
- 当前等待实屏确认启动页面、中文界面、触摸、拍摄和推理；若仍黑屏，立即回滚恢复包。
- [2026-09-05] 启动点亮修正版实机仍黑屏，已回滚恢复包至稳定 OpenVela 基线；回滚写入与校验通过，模型区未改动。ESP-IDF 产品候选冻结，后续仅在电脑端定位启动初始化冲突。
- [2026-09-05] 加速评估：当前 ESP32-P4 已配置 400MHz 双核、250MHz PSRAM；ESP-DL 已使用 P4 专用库，模型为 INT8 split_fast。源码仍使用通用 ImageTransformer，尚未接入 PPA 图像缩放，也未做推理任务与显示任务的资源隔离。已确认 `model.run()` 历史实测约 6.34 秒，故将“单张完整流程 10–20 秒”评为可行但需实机计时，不能仅凭编译结果承诺。
- [2026-09-05] 下一轮电脑端优化顺序：保留稳定 OpenVela 固件；建立离线计时日志；先评估 PPA 缩放/颜色转换，再检查模型加载是否可延后或缓存、显示刷新是否阻塞、任务栈与内存布局；每次只改一个变量，达到稳定且有实测数据后才申请刷写。
- [2026-09-05] 离线检查结果：工程已启用 `CONFIG_SOC_PPA_SUPPORTED=y`，ESP-DL 提供 `register_ppa_srm_client()/resize_ppa()`；但当前模型输入是 `RGB888_QINT8`，PPA 直接支持的是 RGB888/RGB565 等图像格式，不能直接替代现有“缩放+量化”整段预处理。若接入 PPA，需要增加对齐的 RGB888 中间缓冲及一次量化转换，预计主要优化预处理而不是 6 秒级 `model.run()`；先做单变量基准，不直接刷写。
- [2026-09-05] 已完成更低风险的单变量优化候选：将 ESP-DL `param_copy` 从 `false` 改为 `true`，让模型参数复制到 250MHz PSRAM，避免从 Flash 直接取参数造成推理变慢；VM 编译通过，应用大小仍为 `0x2854B0`，SHA-256 `caf5e249cee8e1333bc66b9db8a71e9610f6b8efe687d9a8c8efe1f3db19de41`。尚未刷写，需在后续有明确窗口时单独授权并实测 `model.run()`。
- [2026-09-05] 进一步核查确认：当前 320×320 输入预处理由通用 `ImageTransformer` 完成；PPA 只能直接处理 RGB565/RGB888 等格式，无法直接输出模型需要的 QINT8，因此 PPA 方案需增加中间缓冲和量化步骤，暂不优先。下一次板端测速只需验证 `param_copy=true` 这一单变量，预计准备时间已完成，等待明确板卡测试窗口。
- [2026-09-05] 已为 `param_copy=true` 候选加入 `[PERF]` 分段日志：模型加载、相机采集、预处理（原有日志）、`model.run()`（原有日志）、解码与总阶段均可在一次实机运行中读取；新应用编译通过，大小 `0x285620`，SHA-256 `ed7f24b360c2cd9b1a52505af1c24d543f771e2725630b994ac5403bbec0f395`。未刷写，等待用户通知板端测试。
- [2026-09-05] 在同一候选上增加模型加载前后 heap/最大连续块日志，用于确认 `param_copy=true` 是否挤压相机帧缓冲或任务栈；编译通过，应用大小约 `0x285780`，SHA-256 `119c5cc415f4392ade8651a59afe7538aa7bbeaf3b96b5ca92003ae403eeb1c2`。仍未刷写，板卡继续保持稳定 OpenVela。
- [2026-09-05] 发现工程此前使用调试优化 `-Og`；已在 VM 备份 `sdkconfig.debug-backup-20260905` 后切换性能优化配置，实际编译参数变为 `-O2`。性能构建通过，应用大小 `0x287B40`（2,653,472 字节），SHA-256 `5195f1bb0511678bf7d63ee2bf70a54184c4782a96d1b97c7d5912a1703d0c26`。该候选同时包含 `param_copy=true` 与 `[PERF]` 日志，尚未刷写，等待板端测试窗口。
- [2026-09-05] 核查确认 ESP-DL 组件已使用 P4 专用汇编及组件级 `-O3`；主程序在全局 `-O2` 下运行。已额外构建主程序 `-O3` 候选，应用大小 `0x289A20`，SHA-256 `bcfd43a7e2d889a8c513218f53f819dafb4a06912f7d753a5527deff294666c5`。该候选未刷写，当前不再有安全的纯配置加速项，下一步需要板端实测比较 `param_copy=true`、分段耗时及内存余量。
- [2026-09-05] 完成启动路径低风险优化候选：删除 `app_main()` 中固定 5 秒等待，改为立即进入模型加载；未改变稳定中文版界面、触摸、单帧拍摄、模型参数 `param_copy=true`、分段性能日志或相机流程。VM 编译通过，当前主程序仍带 `-O3`，应用大小 `0x289A00`（2,660,864 字节），SHA-256 `6493a98066b951cbc3d0266fbf72edcded1c1765911f152c02f957ba5bb359a2`。仅完成电脑端核验，未刷写；下一步必须在板端测量启动时间、拍摄时间和 `model.run()`，再决定是否保留。
- [2026-09-05] 完成双核推理候选（未刷写）：确认 ESP-DL 的 `Model::run()` 默认是 `RUNTIME_MODE_SINGLE_CORE`，而 ESP32-P4 支持 `RUNTIME_MODE_MULTI_CORE`；将启动自检和产品推理两处调用改为显式双核运行，同时保留 `param_copy=true`、主程序 `-O3`、性能日志及稳定单帧流程。VM 编译通过，应用大小 `0x289A00`（2,660,864 字节），SHA-256 `d8fce20474756ee981f85bf1a1239d79d99c3d3a74805a3990feef212552f174`。候选已保存至 `artifacts/candidate-multicore-no-delay-20260905`，尚未刷写；必须由板端实测比较单核/双核耗时、内存和稳定性后才能采用。
- [2026-09-05] 完成产品推理路径的转换器复用候选（未刷写）：`ProductActionWorkerContext` 持有单个 `ImageTransformer`，重复点击“标签/缺陷”时复用已生成的缩放坐标映射，避免重复分配/生成；同时新增 `product_preprocess_us`、`product_inference_us`、产品侧 `decode_us` 日志。模型、阈值、输入尺寸、双核模式、中文界面和拍摄流程均未改变。VM 编译通过，应用大小 `0x289B50`（2,661,712 字节），SHA-256 `92cd27cbfeb5494d9ee06a546115d46586d9e7da6f8f868298f02986239624e2`。候选已保存至 `artifacts/candidate-multicore-reuse-transformer-20260905`，未刷写；下一步仍需板端实测确认复用后的耗时与稳定性。
- [2026-09-05] 完成“界面优先、取消上电自检推理”候选（未刷写）：移除 `app_main()` 上电后自动采集+推理及对应结果绘制，保留模型加载、相机/ISP 初始化、中文界面、触摸任务和单帧产品流程；上电后直接进入可操作页面，只有用户点击按钮才触发拍摄/推理。VM 编译通过，应用大小 `0x288F00`（2,658,048 字节），SHA-256 `7a9fc0e0a2ca21a9e8b395e816939f441a6eb58386bb235782a1ea59f43fc11a`。候选已保存至 `artifacts/candidate-ui-first-no-boot-inference-20260905`，未刷写；需板端确认启动亮屏、触摸、拍摄和首次推理稳定性后再采用。
- [2026-09-05] 已获用户“下载模式好了”授权，向 ESP32-P4 `/dev/ttyACM0` 仅刷写 `0x10000` 应用区候选 `candidate-ui-first-no-boot-inference-20260905`；未写入 `0x2000`、`0x8000` 或 `0x410000` 模型区。芯片识别为 ESP32-P4 v3.2，应用写入 2,658,048 字节并返回 `Hash of data verified`，随后硬复位。当前等待用户实屏确认中文界面、触摸和按钮行为；尚未把本次候选标记为验收通过。
- [2026-09-05] 用户反馈“界面优先、取消上电自检推理”应用候选刷写后黑屏；确认芯片可连接后，按授权回滚稳定 OpenVela 恢复包 `recovery-range-0x002000-0x134000.bin` 至 `0x2000`，写入范围未覆盖模型区 `0x410000`。写入 1,253,376 字节并返回 `Hash of data verified`，随后硬复位；等待用户确认稳定基线亮屏。
\n##### [2026-09-05] 主任务栈 8192 候选编译完成（未刷写）
- 在 VM 工程 `/home/max/energy_label_p4_product_runtime` 将 `CONFIG_ESP_MAIN_TASK_STACK_SIZE` 从 3584 调整为 8192，并保留备份 `sdkconfig.main-stack-backup-20260905`。
- `idf.py build` 成功；应用 `build/mipi_isp_dsi.bin` 大小 2,658,064 字节，SHA-256：`f98fdaf54064d1a063264e8037bbd4c8fb7ad50f6ad8ffa5b6f6a5756b13837c`。
- 已核对 `CONFIG_COMPILER_OPTIMIZATION_PERF=y`、`param_copy=true`、`dl::RUNTIME_MODE_MULTI_CORE` 均保留。
- 候选已保存至 `artifacts/candidate-main-stack-8192-20260905`，未刷写，模型分区 `0x410000` 未涉及；当前板卡继续使用已确认亮起的稳定基线。
- 下一步仅在用户明确准备板卡后进行单变量刷写与实测，重点记录启动稳定性、拍摄耗时、`model.run()` 耗时及内存余量；失败立即回滚既有恢复包。
##### [2026-09-05] 主任务栈 8192 候选已刷写，等待实屏确认
- 已确认 ESP32-P4 v3.2 可连接；仅写入 `0x10000` 应用区，未写 `0x2000`、`0x8000` 或 `0x410000` 模型区。
- `mipi_isp_dsi.bin` SHA-256 `f98fdaf54064d1a063264e8037bbd4c8fb7ad50f6ad8ffa5b6f6a5756b13837c`，写入校验 `Hash of data verified`，已硬复位。
- VM 当前仅枚举 `/dev/ttyACM0`，未出现 `/dev/ttyUSB0` 日志口，因此暂不能从串口确认启动日志；需要以实屏确认是否持续显示、触摸是否可用。
##### [2026-09-05] 黑屏候选离线对比结论
- 主任务栈 8192 候选刷写后黑屏，已回滚稳定基线。
- 将同一候选源码恢复为原主任务栈 3584 后重新编译，应用 SHA-256 `7a9fc0e0a2ca21a9e8b395e816939f441a6eb58386bb235782a1ea59f43fc11a`，与此前黑屏候选一致（说明栈值是唯一差异）。
- 因而不能单凭本次实机结果断定 8192 是根因；当前必须先取得日志口或构建带早期启动标记的最小诊断固件，再继续定位。稳定基线已恢复，后续不自动刷写。
##### [2026-09-05] 早期启动标记诊断候选已编译（未刷写）
- 在 `app_main()`、显示等待页、模型构造前后、模型张量就绪、MIPI LDO 和传感器初始化后加入 `[BOOTCHK]` ROM 输出标记，用于定位黑屏卡点。
- 未改变模型、UI、推理参数、分区表或启动流程；VM 编译通过，应用大小 `0x289070`（2,658,416 字节），SHA-256：`3707c616677c76dc37f51fc8bda56264761f9b23da8c7fa059ddfd61f021223d`。
- 候选已保存至 `artifacts/candidate-bootchk-20260905`，尚未刷写。当前板卡保持稳定基线。
- 由于 VM 仍未枚举 `/dev/ttyUSB0`，下一次实机验证前需确认日志通道；若只能使用 `/dev/ttyACM0`，将通过 USB Serial/JTAG 控制台读取 `[BOOTCHK]`。
##### [2026-09-05] 启动诊断候选实测失败并回滚
- 仅刷写 `0x10000` 的 `[BOOTCHK]` 诊断应用后，用户确认屏幕仍黑屏；未能从 `/dev/ttyACM0` 读到启动标记，说明当前 USB 下载通道不能作为运行日志通道。
- 已立即使用恢复包回滚 `0x2000–0x133FFF`，校验通过并硬复位；模型分区 `0x410000` 未改动。
- 结论：黑屏发生在应用早期且缺少可用日志通道；后续不再刷写同类候选，改为使用已验证稳定固件继续功能工作，或待用户提供 UART0 日志线后再诊断。
##### [2026-09-05] 已恢复稳定源码并完成离线重编译
- 已从诊断候选源码移除全部 `[BOOTCHK]` 标记，恢复正常产品源码；`sdkconfig` 恢复 `CONFIG_ESP_MAIN_TASK_STACK_SIZE=3584`。
- VM 编译通过，应用大小 `0x288F00`（2,658,048 字节），与此前已知产品基线一致；未刷写板卡。
- 后续工作转为电脑端性能分析和功能完善，避免继续刷写无法读取日志的黑屏诊断候选。
##### [2026-09-05] 电脑端基准确认
- 已确认本机 PyTorch `2.6.0+cu124` 与 NVIDIA GeForce RTX 4060 Laptop GPU 可用。
- 原始 `models/best.pt` 使用 Ultralytics GPU、`imgsz=640` 单张推理实测约 `8.7–13.6 ms`，5 次平均约 `10.7 ms`（不代表 ESP32-P4 板端耗时）。
- ESP32-P4 侧仍需依赖 `[PERF]` 日志拆分拍摄、预处理、`model.run()` 和解码；当前没有可用日志口，因此不把电脑端数据冒充板端结果。
- 后续优先方向：在电脑端复现 320 输入、INT8/ESP-DL 预处理链，减少重复内存拷贝；任何新固件先编译和备份，再单独申请上板验证。
##### [2026-09-05] 320 输入电脑端仿真基准
- 本机 ONNX Runtime 可见 CUDA/TensorRT 提供器，但 CUDA 执行依赖 `cudnn64_9.dll` 缺失，实际测试出现回退告警；因此本轮“GPU320 约 7.8 ms”不能作为可靠 CUDA 实测，只能作为当前运行环境的参考值。
- CPU 执行 320 输入平均约 `34.9 ms`，测量波动较大；OpenCV 缩放约 `0.20 ms`，RGB/CHW 浮点预处理约 `0.79 ms`。
- 结论：电脑端预处理不是主要瓶颈；ESP32-P4 端应继续以 `[PERF] product_inference_us` 为核心指标，PPA 仅作为后续预处理优化选项，不能宣称能把板端推理压到电脑级毫秒数。
##### [2026-09-05] 稳定源码内存/分区静态核对完成
- `idf.py size` 显示应用 Flash 约 2,581,328 字节，应用分区剩余 37%；DIRAM 使用 13.49%，未见静态内存耗尽。
- 模型分区固定为 `0x410000`、大小 4 MiB；当前构建脚本引用 `energy_label_yolov8n_320_split_fast_int8_p4.espdl`，不会因应用刷写覆盖模型。
- 结构性风险仍集中在 `app_main()` 中模型构造/相机初始化的早期运行阶段，而非链接空间不足；下一步应优先通过稳定日志通道或最小化启动路径验证。
##### [2026-09-05] `param_copy=false` 单变量候选已编译（未刷写）
- 历史稳定源码均使用 `param_copy=false`；当前黑屏候选使用 `true` 会在启动阶段复制模型参数到 PSRAM。
- 已仅将产品源码模型构造参数恢复为 `false`，其余 UI、触摸、拍摄、双核推理和解码完全不变。
- VM 编译通过，应用 SHA-256：`82605bb2ae0b0961bf4171afaf3370baeacbaec9c01b74ec5a9f23b35a1f0dda`；候选保存至 `artifacts/candidate-param-copy-false-20260905`。
- 尚未刷写；下一步如需上板，必须单独确认只写 `0x10000` 应用区并准备回滚。
##### [2026-09-05] `param_copy=false` 候选实测仍黑屏，已回滚
- 用户确认 `param_copy=false` 应用候选刷写后仍黑屏。
- 已回滚稳定恢复包至 `0x2000–0x133FFF`，校验通过，模型区 `0x410000` 未改动。
- 结论：黑屏根因不是 `param_copy` 单一开关；后续停止在完整产品镜像上反复试错，改为基于已亮屏版本逐步恢复模型/相机初始化，或补齐 UART0 日志通道后再定位。
##### [2026-09-05] 开发基线一致性恢复
- 已将虚拟机源码重新同步为本地稳定开发基线，确认 `param_copy=true`、`RUNTIME_MODE_MULTI_CORE`、主任务栈 3584 均一致。
- VM 重新编译通过，应用大小 `0x288F00`（2,658,048 字节），未刷写板卡。
- 当前所有加速候选均保留在独立 `artifacts/candidate-*` 目录，稳定板卡固件不再被实验候选覆盖。
##### [2026-09-05] ESP-DL 双核实现核对完成
- 已检查 ESP-DL 3.3.9 源码：`Model::run(RUNTIME_MODE_MULTI_CORE)` 会把部分大卷积按高度拆分，但 Requantize、部分激活/其他算子仍保留单核路径（源码中明确标注 `TODO`）。
- 因此“双核模式”不是整张 YOLO 图完全并行，实际加速比例必须由板端 `model.run()` 实测确认；不能按核心数直接估算 2 倍。
- 当前源码和模型保持稳定基线，未做新的高风险改动；后续最有价值的加速方向是使用专门针对 ESP32-P4/ESP-DL 的更小输入或重新导出的模型，而不是继续修改 UI/启动流程。
##### [2026-09-05] 小输入分辨率验证集评估完成
- 在 285 张验证集上用原始 `best.pt` 评估：`imgsz=320` 的 mAP50 `0.8665`、Precision `0.9324`、Recall `0.8634`；damage AP50 `0.5048`、wrinkle AP50 `0.7976`、box AP50 `0.4277`。
- `imgsz=256` 的整体 mAP50 约 `0.7883`、Recall 约 `0.8175`，damage AP50 约 `0.4267`、wrinkle 约 `0.7101`、box 约 `0.3026`（相比 320 已明显下降）。
- `imgsz=224` 的整体 mAP50 `0.6748`、Recall `0.6986`，damage AP50 `0.2405`、wrinkle `0.6814`、box `0.2498`，不满足尽量不漏检要求。
- 结论：不采用 224；256 也会明显伤害破损/位置偏差召回。当前最稳妥输入仍为 320，后续加速应转向 ESP-DL 算子/内存路径，而非简单降分辨率。

##### [2026-09-05] OpenVela 规则质检与轻量 AI 辅助首轮离线实现
- 原始数据可直接复用：YOLO 标注的 label（类别 8）用于裁剪标签 ROI；文件名中的 NOR/STA/DAM/WRI 用作第一版正常/污渍/破损/褶皱真值。经核查，YOLO 的 stain 类也出现在 NOR 文件中，故不得直接把该类别当作异常真值。
- 新增 tools/train_lightweight_roi_anomaly.py：以 96×64 标签 ROI 的 144 个灰度分块均值/标准差和 Sobel 边缘特征，训练并导出固定点线性评分器。训练使用固定随机种子、1200 张分层样本；验证保留全部 228 张带明确状态码图片。
- 该版本验证集异常召回为 88.24%，特异性为 58.04%，平衡准确率为 73.14%。整数导出的指标与浮点参考一致量级；当前只能作为“可疑外观异常”辅助信号，不能单独替代最终产品判定。
- 已对 16 单元特征 MLP 和 1,282 参数微型 CNN 做比较，验证结果分别为 72.25% 与 61.47% 平衡准确率，均未优于线性评分器，已淘汰，不进入板端。
- 新增 components/energy_label_rules/lightweight_roi_anomaly.hpp/.cpp：实现 RGB565 ROI 的双线性灰度缩放、144 个整数特征、整数平方根、Sobel 边缘特征和线性评分接口。接口使用 6 KiB 调用方提供的缓冲，不使用动态分配、ESP-DL、TFLite 或浮点运行时；已在 VM 以 g++ -std=c++17 -Wall -Wextra -Werror 独立编译通过。
- 下一步：建立 RGB565 主机一致性测试，将轮廓提取接到稳定 OpenVela 相机帧；随后才生成单变量板端候选。当前未构建或刷写任何板端候选。
- 已完成规则核心接口：components/energy_label_rules 中的 inspection_types.hpp、geometry_rules.hpp/.cpp 定义并实现了 Frame、InspectionResult、EventRecord、原因码、标签几何判定与外观可疑信号合并；VM 独立编译通过。
- 已新增 tools/calibrate_geometry_rules.py，并从 597 张 NOR 训练样本导出初始几何参数到 artifacts/lightweight-roi-ai-20260905/geometry-calibration.json。标签中心中位数约为 x=501、y=507（千分比），面积与长宽比范围已保留；这些仅是离线初值，必须由固定工装的实机合格品重新标定。

##### [2026-09-05] 版本 2.0-profile 实机黑屏，已回滚
- `2.0-profile` 仅在应用区 `0x10000` 刷写按需 ESP-DL profiling 候选；用户实屏反馈为黑屏，候选未通过验收，禁止继续以该映像进行测试。
- 已恢复稳定包 `artifacts/flash-backup-pre-espdl-migration-20260901/recovery-range-0x002000-0x134000.bin`，SHA-256 `fdf5de8d371f45c0b335c29794413d6f53acea3fa728a8197a278a6fd0161b0c`。
- 回滚实际写入范围为 `0x2000–0x133FFF`（1,253,376 字节），`write-flash` 返回 `Hash of data verified`，独立 `verify-flash` 返回 `Verification successful (digest matched)`，随后已硬复位。
- 本次回滚未写入模型区 `0x410000`，也未写入候选模型；当前板端回到稳定恢复基线。后续先以实屏确认亮屏和触摸，未经可用日志通道与单变量验收，不再刷写完整产品/性能候选。

##### [2026-09-05] 加速候选与稳定 OpenVela 的兼容性门槛已确认
- 对比 `2.0-profile` 与稳定开发源码，profiling 本身仅增加编译期开关以及首次推理后的 `profile_memory()`、`profile_module(true)` 调用；它不是本次跨系统黑屏的唯一可归因变量。
- 关键差异是运行体系：当前板端恢复包是稳定 OpenVela 映像布局；`2.0-profile` 是 ESP-IDF/ESP-DL 产品应用，体积为 2,944,320 字节，不能当作 OpenVela 恢复映像的可替换应用段。故此候选已冻结，禁止重刷。
- OpenVela 侧已完成的 ABI 证据表明：现有底层 HAL 为软浮点 ABI，官方 ESP-DL P4 `libfbs_model.a` 为单精度硬浮点 ABI；链接会报 `can't link single-float modules with soft-float modules`。历史全系统硬浮点候选也已在 `nx_start()` 后启动阻断，不能作为加速产品路线。
- 后续加速决策固定为：保持当前 OpenVela 稳定基线，不再混刷 ESP-IDF ESP-DL 应用；若重启 ESP-IDF 加速产品路线，必须使用与其 bootloader、分区表和模型分区一致的完整独立映像，并先单独建立可回退的整套刷写/验收流程。
- 虚拟机当前已同时枚举下载口 `/dev/ttyACM0` 与 UART0 日志口 `/dev/ttyUSB0`。这消除了“没有运行日志”的测试前置阻塞；但在确定系统路线前，不因此自动刷写任何候选。

## 2026-09-06 v2.10-ui-owned-cyan-tolerant

- 放宽青色标签定位阈值，针对“仅底部红边 / label_not_found”。
- 构建成功，镜像 303548 bytes，SHA-256 `3513c5b09f449cab4b4a67028b1adcb19def8a0206cf637645dc8088233a9c5b`。
- 仅写应用区 `0x2000..0x4C1BB`，模型区 `0x410000` 未触碰。
- 已完成刷写与 verify-flash；待实机观察标注框与稳定性。

## 2026-09-06 v2.11-ui-result-caption

- 拍摄后自动标签识别结果增加可见状态提示：绿色 `LABEL OK` / 红色 `NO LABEL`，避免误以为缺少 LABEL 按钮。
- 构建与完整链接通过；镜像 303820 bytes，SHA-256 `48a1cde2497ead368078cdaeff33d53d915f63dd42b2046a2e9b716e7cb47b22`。
- UI ownership 与 camera cleanup 审计通过；模型区 `0x410000` 未触碰。
- 待刷写并进行一次实机观察。

## 2026-09-06 v2.12-review-sharpen

- 针对回看图“有点糊”，增加仅作用于回看帧的轻量 RGB565 锐化处理。
- 不改变采集分辨率、推理输入、触摸、UI 所有权或模型区。
- 构建、完整链接、UI ownership 和 camera cleanup 审计通过；镜像 304100 bytes，SHA-256 `6f80730a27d817554b9902190aa4842c6ca5424e9476b06fc6ade0cc7d3dba6a`。
- 待刷写后由用户观察清晰度和稳定性。

## 2026-09-06 v2.13-two-tap-live-preview

- 恢复实时取景交互：首次点击拍摄区域进入持续取景，第二次触摸释放锁定当前帧并恢复 UI。
- 使用既有 CSI/ISP RGB565 + DSI live 会话，不更改模型、规则或采集分辨率。
- 构建、完整链接、UI ownership 和 camera cleanup 审计通过；镜像 304512 bytes，SHA-256 `d0bc180dfa857e86dfb7c7a92706764395cfdd5d92a02c72f53fcd75e1ef1d0a`。
- 待刷写后实机验收实时画面、第二次点击锁定、UI 恢复和触摸稳定性。

## 2026-09-06 v2.14-live-preview-rule-result

- 将实时取景的第二次点击锁定接入现有轻量规则：锁定帧自动识别，回看图显示绿色/红色标记及结果底栏，然后恢复 UI。
- 不引入模型推理，也不修改触摸校准、采集分辨率或模型区。
- 构建、完整链接、UI ownership 和 camera cleanup 审计通过；镜像 304608 bytes，SHA-256 `30563c50f02d1d2b7fff20a2ebf2640c11c1f40494c8239b2dc00b5080b2f826`。
# 2026-09-06 v2.21-real-roi-only candidate

- User observation: `v2.20` always rendered the green presence box in the
  central region even after the real label moved. Root cause: the fixed-jig
  high-contrast fallback returned a synthetic central ROI, rather than a
  measured label envelope.
- Removed the fallback from the actual camera locator and appearance pipeline.
  A green review overlay now requires a real color-envelope measurement; an
  unlocated frame remains red rather than reporting a misleading central box.
- Added `tests/test_v220_fixed_roi_regression.cpp`. It fails on `v2.20` with
  central high-contrast non-label texture and passes after the correction; it
  also verifies an off-centre cyan target returns its off-centre coordinates.
- Full OpenVela build passed. Candidate image is `305088` bytes, SHA-256
  `9ddc0fa2afcfe321968a9384e67c3ab745ad72dc69c7ad5408f8880c40f1f54c`,
  for application-only address `0x2000..0x4C7BF`. The `0x410000` model
  partition is not touched. Board update completed through `/dev/ttyACM0`;
  write verification returned `Hash of data verified` and independent
  `verify-flash` returned `Verification successful (digest matched)`.
  Pending human acceptance: move the target left and right and confirm the
  green box follows the target.

# 2026-09-06 v2.22-green-real-roi

- `v2.21` correctly removed the synthetic central box but the physical green
  label remained red because the locator only accepted red/blue-dominant
  RGB565 candidates. A regression test first reproduced this failure.
- The locator now accepts green-dominant pixels after normalizing RGB565 green
  from six bits to the five-bit red/blue scale. White pixels therefore do not
  pass merely because their raw green number is larger.
- Regression PASS: central high-contrast non-label texture is rejected, and
  both off-centre blue/cyan and green labels return their measured envelopes.
  Full OpenVela link PASS.
- Image SHA-256 is `e43e0e7baddfd5d93515c957dc7949cf60b8a8cdca70552bcf1e2c79e094f831`,
  size `305140`, application range `0x2000..0x4C7F3`; model partition
  `0x410000` was not written. Write verification and separate `verify-flash`
  both passed. Pending acceptance: the physical label must move the green box
  when placed at two distinct positions.

# 2026-09-07 v2.23-connected-roi candidate

- Physical gallery evidence showed a clear central cyan/blue label with a
  rectangular border, while `v2.22` still returned red. The failure is no
  longer treated as a color-only problem: the prior locator merged every
  matching pixel in the full frame, then rejected the resulting oversized
  envelope.
- `v2.23` tracks bounded row-connected coarse candidates and selects the
  highest-pixel candidate that satisfies the label rectangle shape. It then
  refines only that candidate. There is no fixed centre fallback.
- New regression coverage verifies that an independent cyan distractor does
  not swallow a valid cyan label, alongside the existing central-noise and
  off-centre blue/cyan/green cases. Host regression, complete cross-build and
  preview error-path audit passed.
- Candidate SHA-256 `1277c9e6c905829594038158d32614362eaa654ee224d8b6b39c51655cadc0d9`,
  size `305680`, application-only range `0x2000..0x4CA0F`; model partition
  `0x410000` remains excluded. Board flash completed through `/dev/ttyACM0`;
  write-side verification returned `Hash of data verified` and independent
  `verify-flash` returned `Verification successful (digest matched)`. Pending
  screen acceptance: the green box must cover the physical label rather than
  a synthetic central region.

# 2026-09-07 v2.24-calibrated-real-roi

- User reported that `v2.23` produced a green box but it was not reliably on
  the label. A new regression case reproduced the ranking flaw: a larger
  same-colour edge rectangle won over a central valid label.
- Candidate ranking now applies the fixed-workstation centre window before
  choosing the highest-pixel, label-shaped connected component. These values
  constrain selection only; the returned frame overlay continues to use the
  actual measured component bounds.
- Full regression, cross-build, and preview error-path audit passed. Image
  SHA-256 `bbbb7d30dbc4f2c69aeb4b5bee49fe6c1435e69beb4a1cf918b8a9e27577653b`,
  size `305792`, was written only to `0x2000..0x4CA7F`; `0x410000` model
  region was not accessed. Write verification and independent `verify-flash`
  both passed. Pending screen acceptance on the supplied central-label scene.

# 2026-09-07 v2.25-bright-paper-roi

- `v2.24` still failed to identify the supplied physical label, despite its
  visible cyan/blue outer border. With no runtime UART export channel, the
  next measured feature derives from the gallery evidence: the label contains
  a dense, high-brightness white paper rectangle against a darker fixture.
- A centre-window bright-paper fallback now runs before color candidates. It
  requires both a valid label shape and at least 45% bright-pixel density,
  rejecting sparse text/highlight patterns. Its result remains measured and is
  expanded by 22% to include the exterior border; no fixed box is returned.
  Appearance scoring is bypassed for this new, uncalibrated ROI type.
- Regression, cross-build and preview audit passed. Image SHA-256
  `443cbc9e0dd4e89b7e0d20ec5888851d7133082765cc259bede7043a1bb4f53a`,
  size `306260`, was written only to `0x2000..0x4CC53`; the `0x410000` model
  partition was not accessed. Write verification and independent
  `verify-flash` both passed. Pending physical-label acceptance.

# 2026-09-07 v2.25 rejection and rollback

- User reported that `v2.25` drew a box in the middle regardless of whether a
  label was present. This is a false-positive defect: the centre bright-paper
  fallback measured background rather than a label and is not valid.
- Immediately rolled back to `v2.22-green-real-roi`, SHA-256
  `e43e0e7baddfd5d93515c957dc7949cf60b8a8cdca70552bcf1e2c79e094f831`.
  Only `0x2000..0x4C7F3` was written; write-side verification and independent
  `verify-flash` both passed. Model partition `0x410000` was not accessed.
- Freeze all heuristic locator candidates. The only USB device currently
  exposed to the VM is `/dev/ttyACM0`, usable for ROM flashing but not for
  OpenVela runtime output. Do not claim raw-frame export until a runtime
  transport is established, for example a passed-through UART0/CH340 port or
  an explicitly verified on-board storage/export path.

# 2026-09-07 v2.26 raw-pixel diagnostic

- User requested continued work after false-positive candidates were frozen.
  Runtime evidence shows OpenVela is configured for UART0 console and syslog
  at `/dev/ttyS1`, but VMware currently exposes only the ESP USB download/JTAG
  port `/dev/ttyACM0`; it cannot collect runtime image data.
- Built a diagnostic-only review overlay on top of the v2.22 baseline. On a
  label-not-found result only, green 4-pixel cells show pixels meeting the
  actual board color predicate, and yellow cells show high-brightness pixels.
  The diagnostic does not alter classification, Gallery's raw locked frame,
  touch, preview, model storage, or any success result.
- Restored-baseline locator regression, full cross-build and preview audit
  passed. Image SHA-256 `32e7d2c077412fb424f3839542598b1fabc093192f8df6eba7a04d65bd65409c`,
  size `305408`, was written only to `0x2000..0x4C8FF`; model region
  `0x410000` was not accessed. Write verification and independent
  `verify-flash` both passed. Pending diagnostic screenshot on the actual
  target scene; do not adjust decision thresholds before interpreting it.

# 2026-09-07 PC 多分类缺陷验收仿真

- 按最新验收口径，污渍、破损、褶皱三类缺陷均要求至少有一部分样本被正确识别；不要求同类每张样本都命中。
- 新增 `tools/render_pc_p4_multiclass_review.py`，使用与板端一致的 RGB565 特征和一对多线性分类头，生成中文结果图及类别分数。
- 增加保守分数间隔规则：最高类别与次高类别间隔小于 `0.75` 时显示“检测到缺陷，类型待确认”，避免把低置信结果伪装成确定类别。
- 已从 228 张验证样本中生成正常、污渍、破损、褶皱各一张正确样例，并额外生成不确定样例；输出位于 `artifacts/pc-rgb565-rules-validation-20260907/multiclass-previews/`。
- 多分类混淆矩阵和召回率详见 `defect-multiclass-report.json`；当前验证集总体准确率 74.6%，污渍 68.8%、破损 40.6%、褶皱 52.4%，三类均有可复现真阳性。
- 本次仅修改电脑端仿真与文档，未刷写板端；当前实机仍为 `v2.38-chinese-review-ui`。

# 2026-09-07 RGB565 可部署特征增量实验

- 在不改变上传图片、标签定位、中文 UI 或板端运行框架的约束下，比较了四种固定 `96x64` RGB565 ROI 线性多分类特征：灰度基线、灰度加颜色、灰度加水平/垂直纹理方向、三者组合。
- 最佳候选为“灰度 + RGB565 色彩网格 + 水平/垂直纹理方向”，共 384 维，验证集准确率为 83.8%，相较灰度基线 82.0% 提升 1.8 个百分点。
- 最佳候选正常误报率 9.1%（基线 10.5%）；污渍召回保持 75.0%，破损由 62.5% 提升至 65.6%，褶皱由 71.4% 提升至 76.2%。
- 报告：`artifacts/pc-rgb565-rules-validation-20260907/defect-feature-benchmark.json`；工具：`tools/benchmark_p4_defect_features.py`。
- 该提升不足以直接宣称产品精度已达标，未生成或刷写新固件。下一阶段应针对破损/褶皱增加更具差异的训练样本并保持独立验证集，再判断是否将 384 维候选固化进板端。

# 2026-09-07 受控演示样本集 v1

- 为可复现演示建立受控样本集，而非声称存在“绝不误判”的通用数据集。样本全部来自未参与训练的验证集，并要求灰度基线和增强 RGB565 特征两套独立线性分类器均正确、且类别领先间隔均不小于 1.0。
- 已选出正常、污渍、破损、褶皱各 3 张，共 12 张，并生成固定 `1024x600 RGB565 little-endian` 的 OVIP 上传包。每张均记录来源文件哈希、OVIP 载荷哈希、包哈希、预期类别和双模型领先间隔。
- 资产目录：`artifacts/p4-controlled-defect-demo-v1/`；清单：`manifest.json`；生成工具：`tools/build_p4_controlled_demo_set.py`。
- 该集合仅可用于受控演示和回归验证，不能用来宣称任意现场图片不会误判，也不替代独立精度评估。本次未刷写板端。

# 2026-09-07 多分类板端接口准备

- 当前二分类结果结构缺少缺陷类型字段；已生成四分类固定点模型头 `artifacts/p4-multiclass-model-v1/multiclass_model.hpp`，类别顺序固定为 `NOR/STA/DAM/WRI`，使用现有 144 维 RGB565 灰度特征，验证集准确率 82.0%。
- 模型报告：`artifacts/p4-multiclass-model-v1/multiclass_model.json`。本次只生成参数和接口准备文件，尚未接入固件、尚未刷写板端。
- 下一步是在 staging 工程中扩展结果结构（类别、四路分数、类型待确认状态），补充主机 C++ 回归和中文 UI 排版检查；通过后候选版本从 `v2.39-` 开始。

# 2026-09-07 v2.39-multiclass-demo-ready 候选构建

- staging 已接入四分类固定点模型调用：新增类别编号、四路分数和类型待确认字段；原二分类异常结果、定位、触摸、上传图片流程保持不变。
- 虚拟机 ESP32-P4 交叉编译和链接通过，生成候选镜像 `artifacts/candidate-v2.39-multiclass-demo-ready-20260907/nuttx-v2.39-multiclass-demo-ready.bin`。
- SHA-256 `E8D5C928ECDB7C6C3D632DA6DD877BBB86411ACD664180FA5B7E40DCAFA72272`，大小 308048 字节；预计仅写应用区 `0x2000`，模型区 `0x410000` 与图片区 `0xEB3000` 未修改。
- 构建报告：`artifacts/candidate-v2.39-multiclass-demo-ready-20260907/BUILD-REPORT.md`。
- 当前状态：候选未刷写；中文结果页仍需接入类别文字/颜色，并完成主机回归后才能请求板端观察。

# 2026-09-07 v2.40-multiclass-cn-result-ui 候选构建

- 中文结果页已接入四分类显示：正常、污渍、破损、褶皱；分数间隔不足时显示“检测到缺陷，类型待确认”。
- 补齐结果页中文字形，定位框、触摸、上传图片、图库和原二分类异常判断保持不变。
- ESP32-P4 虚拟机交叉编译、链接和镜像生成通过。候选镜像：`artifacts/candidate-v2.40-multiclass-cn-result-ui-20260907/nuttx-v2.40-multiclass-cn-result-ui.bin`。
- SHA-256 `59AD9820B0B3F7CF7480CD63EA2248DE0504D8550264B081B7909348DA18504D`，大小 308148 字节；仅计划写入 `0x2000` 应用区，模型区 `0x410000` 与图片区 `0xEB3000` 未修改。
- 构建报告：`artifacts/candidate-v2.40-multiclass-cn-result-ui-20260907/BUILD-REPORT.md`。当前未刷写实机，等待后续集中验证授权。

# 2026-09-08 v2.40 实机刷写与完整性校验

- 已确认 VMware 中 `/dev/ttyACM0` 为 ESP32-P4（MAC `e8:f6:0a:e3:a9:5c`），并将 `v2.40-multiclass-cn-result-ui` 写入应用区 `0x2000`。
- 写入范围 `0x2000..0x4D3B3`，大小 308148 字节；模型区 `0x410000` 与图片区 `0xEB3000` 未访问。
- 本地与 VM 包 SHA-256 均为 `59AD9820B0B3F7CF7480CD63EA2248DE0504D8550264B081B7909348DA18504D`。
- 写入侧返回 `Hash of data verified`；独立 `verify-flash 0x2000` 返回 `Verification successful (digest matched)`；刷写后已硬件复位。
- 当前等待一次集中屏幕验收：UI 持续显示、触摸/图库可用、上传图后结果页能显示中文缺陷类别或“类型待确认”。

# 2026-09-08 v2.41-demo-carousel 实机刷写

- 为解决单图上传槽无法切换的问题，在原有 `0xEB3000..0xFFFFFF` 图片槽中定义四图轮播包：正常、污渍、破损、褶皱各一张，源图缩放为 `512x300 RGB565`，板端读取后放大为 `1024x600` 显示。
- 轮播包头部、4 个目录项、4 段 SHA-256 和总长度均已在电脑端验证；轮播包大小 1229056 字节，小于已验证槽大小 1363968 字节。
- 图库界面增加左下“上一张”、右下“下一张”、底部中间“返回”；旧单图 OVIP 上传包仍兼容，单图模式保持点击返回。
- `v2.41-demo-carousel` 应用 SHA-256 `D9698CD3E9447C39E4CFC5BC82ED8D06979FDD1B3EC81349116024C5D01A4009`，写入 `0x2000..0x4D8E7`；四图包 SHA-256 `2BB38CAE3C9E2C7A36503FD63136683AD86E6ADB60CB000B53EDFFE5AE5C00C3`，写入 `0xEB3000..0xFDFFFF`。
- 两段写入均返回 `Hash of data verified`，两次独立 `verify-flash` 均返回 `Verification successful (digest matched)`，随后已硬复位；模型区 `0x410000` 未访问。
- 当前等待实屏验收：UI 是否正常、图库能否显示首张、左右按键能否切换四张、底部中间能否返回。

# 2026-09-08 v2.42/v2.43 轮播启动修正

- `v2.41` 上电进入红色图片读取失败兜底；先以 `v2.42` 改为上电只显示稳定主 UI、图库按下后才读取轮播，应用写入及校验成功，图片区未改写。
- 用户反馈图库点击无响应后，检查实际启动脚本发现它传入的是 `camera_diag --touch-ui-live-delayed`，而应用只识别 `--touch-ui-live`，所以触摸循环没有真正运行。
- `v2.43-carousel-touch-startup-fix` 将 `--touch-ui-live-delayed` 作为正式触摸常驻模式处理；应用 SHA-256 `4F4219EC111031C9BA5C27488E099DDFF97BCBE7D99C55D2F4C48BB116974B2A`，写入 `0x2000..0x4D693`，写入和独立校验均通过，随后硬复位。
- `0xEB3000` 四图轮播包和 `0x410000` 模型区均未改动。当前等待实屏确认：点图库是否打开轮播并显示导航按钮。

# 2026-09-08 v2.44-carousel-dma-refresh 实机修正

- 用户报告：轮播图库打开后，第 2 张没有反应。静态回归检查稳定复现了根因：`ui_gallery_select_image()` 已载入下一张图片并重绘导航条，但遗漏了将修改后的 PSRAM 帧缓冲区同步给正在读取它的 DSI GDMA。因此触摸事件即使被处理，面板也可能持续显示第 1 张。
- 修正：图片切换、规则叠加和导航重绘完成后，显式执行 `esp_cache_msync(..., ESP_CACHE_MSYNC_FLAG_DIR_C2M)`，使 DSI DMA 在下一轮读取中看到新内容。未改变触摸坐标、轮播包格式、模型、相机或主 UI 路径。
- 源码级回归由红变绿：切换函数现在必须包含针对 `g_camera_gallery_frame_rgb565` 的 C2M 同步。完整 ESP32-P4 交叉编译、链接和镜像生成通过。
- 已写入并独立验证：`artifacts/v2.44-carousel-dma-refresh/nuttx.bin`，SHA-256 `70524352AB92D55E9D5A5EDEF253450F82D57EB54BB5C5954BE91865B9575A0C3`，308,900 字节；仅写入 `0x2000..0x4D693`。写入侧返回 `Hash of data verified`，独立 `verify-flash` 返回 `Verification successful (digest matched)`，随后已硬复位。
- 未访问图片轮播区 `0xEB3000` 或模型区 `0x410000`。待实屏确认：打开图库后，右下“下一张”或右半屏点击应切到第 2 张；左半屏应回到上一张，底部中间“返回”应回到主 UI。
# 2026-09-09 v2.60-energy-roi-alloc-diag pre-flash record

- Candidate purpose is limited to TFLite Micro embedded-model parsing and tensor-arena allocation. It is not connected to UI, gallery, camera, or automatic inference.
- Candidate binary: `nuttx-v2.60-energy-roi-alloc-diag.bin`; SHA-256 `3e97710860f3fc584ac6106149d92fdf2b6dfa1d9bc88564e973212976092f9e`; size `405004` bytes; planned write range `0x2000..0x64e0b` only.
- Protected model region `0x410000` and gallery region `0xEB3000` are excluded from the write plan.
- Before any write, a read-only rollback backup covering `0x2000..0x64fff` was captured on the VM in `backups/v2.60-prewrite-app/`. USB transfer was unstable for larger blocks near `0x54000`; the affected part was successfully captured as individually hashed 1 KiB blocks. The backup has not been used and no candidate write has occurred at this entry point.

# 2026-09-09 v2.60-energy-roi-alloc-diag board write

- `v2.60-energy-roi-alloc-diag` was written only to `0x2000..0x64e0b` and reset by the flasher. The write-side hash verification returned `Hash of data verified`; a separate `verify-flash 0x2000 nuttx.bin` returned `Verification successful (digest matched)`.
- The protected model region `0x410000` and gallery region `0xEB3000` were not written or erased. Pending required board observation: stable Chinese UI must remain continuously visible before the manual allocation diagnostic is run.

# 2026-09-09 v2.61-energy-roi-invoke-diag board result

- `v2.61-energy-roi-invoke-diag`, SHA-256 `bab16f74bfc9859f2735a831f04fd69b5307076559308ba0ee022433b7b0b1e4`, size `405288` bytes, was written only to `0x2000..0x64f27`. Write-side verification and independent `verify-flash` both passed. No model or gallery partition was touched.
- Manual UART diagnostic passed model parsing and tensor allocation: `arena_used=60436`, INT8 input `27648` bytes, INT8 output `5` bytes. Five deterministic real-board invokes completed with identical output (`10,-6,36,33,-128`) and elapsed times `680/690/690/690/690 ms`.
- This is a bounded synthetic-input execution result, not a claim of end-to-end gallery accuracy. It establishes that TFLite Micro INT8 is ABI-compatible and that the current model kernel is not the source of the prior 10--20 second delay. Next work is limited to offline integration of the existing ROI path, then a separately versioned board candidate.

# 2026-09-09 v2.62-energy-roi-product-path board write

- Added a separately callable RGB565 ROI-to-96x96 INT8 inference function and a non-boot `--energy-roi-selftest` command. Existing UI, camera, gallery and capture paths are unchanged.
- Candidate SHA-256 `54031f1be6cf00546857d3d8208198c7dfcc7ad73ba450ab5f1a88e292db4fe7`, size `406388` bytes; written only to `0x2000..0x65fff`. Both esptool write verification and independent `verify-flash` passed. Model `0x410000` and gallery `0xEB3000` were not touched.
- Pending UART self-test result and one screen stability observation before any product-path wiring.

# 2026-09-09 v2.62-energy-roi-product-path self-test

- UART command `camera_diag --energy-roi-selftest` completed successfully with `status=0`, `level=1`, and `elapsed_ms=690`. This includes RGB565 ROI conversion, 96x96 INT8 tensor preparation, and one model invoke on the target board.
- The command is manual-only and uses a deterministic synthetic frame; it does not alter capture, gallery, touch, or result UI behavior. Awaiting the user's screen/function check before the next application candidate.

# 2026-09-09 v2.63-energy-roi-integrated board write

- The validated ROI energy inference is now called after successful existing rule-based label localization. It reports `[UI_ENERGY] level=N elapsed_ms=...`; label-not-found behavior and all UI/gallery/touch/capture ownership remain unchanged.
- Candidate SHA-256 `4728ec9f9c946d489ea0123c4170b0d729914753c60aac380662fccb86f0d0bb`, size `406468` bytes; written only to `0x2000..0x65fff`. Write-side and independent verification passed. Model and gallery partitions were not touched.
- Awaiting one board screen/function check, then the next step is to exercise a real selected gallery image and compare the reported energy level with the expected sample.

# 2026-09-09 v2.64-energy-level-overlay board write

- Fixed the missing on-screen energy level by storing the successful ROI inference level in the existing result structure and drawing Chinese `能效等级` plus a digit directly in the existing review footer. No new UI state API was introduced.
- Candidate SHA-256 `79f8cc32f32f1021ac1d04f4d8af46deddbc7176865c6359692deabba068252b`, size `406580` bytes; written only to `0x2000..0x65fff`. Write-side and independent verification passed; model and gallery partitions were untouched.
- Required board check: open a gallery image with a valid label, enter the result/review view, and confirm the bottom row now shows `能效等级` with a number without overlap.

# 2026-09-09 v2.65-energy-letterbox-preprocess board write

- Root cause of misclassified levels was confirmed offline: the model was trained with aspect-preserving ROI letterbox preprocessing, while the board was directly stretching the ROI to a square. Direct stretch accuracy was only about 51% on the held-out set.
- Board preprocessing now matches the validated model contract: RGB565 ROI, aspect-preserving fit, black padding, and bilinear sampling. Candidate SHA-256 `97cfd15a0dc0a8266963c9e53ef01b9ad5c5a361e9883858a480a05df7ef4091`, size `407376` bytes; written only to `0x2000..0x65fff` with both verifications passing.
- Model and gallery partitions remain untouched. Required board check: run one known gallery sample and verify the displayed energy level against its expected label.

# 2026-09-09 v2.66-energy-inner-roi board write

- Host simulation reproduced the board contract and showed the remaining error source: the locator's 12% outer margin reduced held-out energy accuracy from about 87.4% (tight ROI) to about 75%. The energy model now uses a centered 80% inner ROI for inference while the visible annotation box remains the measured full ROI.
- Candidate SHA-256 `257ada25f25d5fd50ed645c142cb99c33407c04398712ef2bba6c1aeb8f9ed72`, size `407412` bytes; written only to `0x2000..0x65fff`, with write-side and independent verification passing. Model and gallery partitions were untouched.
- Required board check: test a known gallery sample and compare the displayed level; this candidate is specifically intended to correct the prior margin-induced mismatch.

# 2026-09-09 v2.66 accuracy diagnosis (no flash)

- Reproduced the reported poor energy-level output with a board-path host
  audit: source image -> 336x200 letterbox -> RGB565 -> 1024x600 panel ->
  cyan locator -> 96x96 model input.
- The current locator ROI overlaps the annotated energy ROI by only about
  44% IoU on 285 validation samples. With the current model contract, energy
  accuracy is about 41%; changing the locator expansion from 0% to 60% only
  reaches about 53%, so threshold/margin tweaking is not a reliable fix.
- Added `tools/audit_energy_locator_contract.py` and reports under
  `artifacts/p4-energy-gallery-audit/locator-fixed-*.json`.
- No firmware was built or flashed for this diagnosis. The next candidate
  must first replace or augment the locator so its ROI matches class-8
  training geometry, then pass the offline audit before board testing.
- A first locator-matched retraining run (`tiny-energy-cnn-v9-locator`) reached
  75.8% held-out accuracy, materially above the 41% board-path baseline, but
  ONNX export was blocked by the VM Python environment's broken `onnx` DLL;
  no model or firmware candidate was produced.
- Board-clarity stress test completed without flashing: native 1024x600 RGB565
  path scored 73.3%; Gaussian blur sigma 1/2/3 scored 74.0%/74.7%/76.5%;
  effective 512x300 scored 73.3%; effective 336x200 scored 79.6%. These are
  not a reliable precision pass—the apparent gain at lower resolution comes
  from locator geometry changing, not improved visual information. Treat the
  model as below acceptance until real board frames or a corrected ROI contract
  are available.
- ONNX export was recovered using the system Python 3.12 environment after
  installing `onnxscript`. The exported locator-matched model now reaches
  73.3% on the full board-path audit (285 samples, margin 12%). TFLite INT8
  conversion is still pending because the model venv has no TensorFlow; no
  board image has been built or flashed.
- TensorFlow CPU and `onnx2tf` are now available, but the first conversion
  exposed two tooling issues: the converter wrapper passed an invalid
  `-ioqd int8` flag (fixed to `-oqd int8`), and the dynamic-batch exported graph
  has an incompatible reshape during representative calibration. The model
  remains host-validated only; no firmware candidate or flash operation was
  performed.
- Re-exported the model with a fixed batch using the legacy TorchScript ONNX
  path. Full-integer TFLite conversion now succeeds; the resulting
  `best_fixed_full_integer_quant.tflite` is 75.8% accurate on the 285-sample
  locator-path validation (input INT8 scale 1/255, output INT8). This is an
  offline pass only; C++ tensor-layout/parity and board integration gates are
  still pending, so no `v2.67` image has been flashed.
- Generated the fixed-batch full-INT8 asset and checked its TFLite contract:
  NHWC INT8 `[1,96,96,3]` input, 5-byte INT8 output, and only PAD/CONV2D/
  TRANSPOSE/RESHAPE/FULLY_CONNECTED operators. The staging assembly now points
  to `energy_roi_v9_locator_int8.tflite`; the resolver already covers these
  operators. VM cross-build and board write remain pending.
- Compared the older v2.30 blue-dominant locator with the current locator on
  285 board-path frames. Mean annotation IoU was 42.3% for the old locator
  versus 44.1% for the current locator, so replacing the current locator would
  not be a dependable improvement. Comparison images are in
  `artifacts/p4-energy-gallery-audit/locator-compare/`; no firmware change or
  flash was made.
- Separated the display-box concern from the inference ROI in staging. The
  candidate display box retracts 6% of its height at both the top and bottom;
  energy inference, position calculations, and the 12% safety-margin ROI are
  unchanged. Host preview is at
  `artifacts/p4-energy-gallery-audit/y-tight-preview/y-tight-contact-sheet.jpg`.
  This has not been cross-built or flashed.
- Tightened the staging-only display box further: it now retracts 10% of its
  height from both the top and bottom while leaving the inference ROI intact.
  The existing product review UI already renders both a Chinese defect result
  (`外观状态`: normal/stain/damage/wrinkle) and `能效等级` on the same footer.
  A ten-sample preview with both annotations is at
  `artifacts/p4-energy-gallery-audit/v9-review/demo10/y10-energy-defect-review.jpg`.

# 2026-09-10 v2.68-y-tight-energy-defect board write

- Restored the VM's existing Espressif RISC-V toolchain through user-local
  compatibility aliases; cross-compilation, linking and image generation
  completed successfully.
- `v2.68-y-tight-energy-defect` SHA-256
  `dae0c9d76bed9d88d7708f538b272a20be39eab2bff27169d02417276e78ea2a`,
  size 407520 bytes, was written only to `0x2000..0x65f3f`. Both write-side
  hash verification and a separate `verify-flash` passed.
- Model partition `0x410000` and gallery partition `0xEB3000` were not
  written. This version uses the locator-matched INT8 model, retains the full
  inference ROI, retracts only the displayed box vertically by 10% at each
  side, and keeps existing Chinese defect and energy-level output.
- Pending board acceptance: stable boot/UI, gallery open, one known sample's
  green box visibly tighter vertically, and simultaneous visible defect plus
  energy-level result.

# 2026-09-10 v2.69-v9-demo10 gallery synchronization

- Diagnosed the reported energy mismatch: the board gallery was still using
  the earlier controlled set. Re-running the deployed v9 model with the exact
  C++ RGB565 preprocess on that old set produced only 5/10 correct results.
- Built a new ten-image OVIP4CAR gallery containing the v9-selected samples:
  two samples each for levels 1--5, including stain, damage and wrinkle
  examples. Exact C++ preprocess plus the deployed INT8 TFLite model passes
  all 10/10 expected levels before flashing.
- Gallery package `p4-v9-demo10.ovip`, SHA-256
  `77033cc8d36d0cdfafbe1c2a81741f4eb13baf0ff328800d7dde115359cc3920`,
  size 1,345,024 bytes, was written only to `0xEB3000..0xFFB5FF`.
  Write-side verification and independent `verify-flash` passed. Application
  `0x2000` and model region `0x410000` were not touched.
- Pending physical acceptance: Gallery shows ten source images; run each to
  confirm displayed energy levels match 1,1,2,2,3,3,4,4,5,5 in carousel order.
- VM cross-build is currently blocked because the expected
  `riscv32-unknown-elf-gcc/g++` toolchain is not on the VM PATH. The host-side
  model remains usable: generated per-sample annotated review images and a
  contact sheet at `artifacts/p4-energy-gallery-audit/v9-review/`, with 73.3%
  accuracy over 285 validation samples. No flash operation was attempted.
