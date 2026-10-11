# Quest cutscene camera-anchor calibration

The stereo first-person cutscene camera rides the animated head bone when an
animated actor is available. The camera keeps the existing 4 cm lift and adds a
profile offset stored in the actor's local horizontal frame. Exact model-name
profiles take priority. If a model name is not recognized, it inherits the
existing `PLAYER` profile when the actor is the live player ped, or the existing
`CSPLAY` profile when the actor is a staged cutscene actor. New replacement names
therefore keep the established framing without requiring a new calibration first.
Profiles are keyed by the normalized model name, not by cutscene name, so the
same actor model reuses its offset in later scenes. Four calibrations exported
from the Quest are compiled in as defaults:

| Model | Model index | Side (cm) | Forward (cm) | Height (cm) |
|---|---:|---:|---:|---:|
| `CSPLAY` | 109 | -1.278 | 9.788 | 2.942 |
| `PLAYER` | 0 | -3.232 | 6.593 | 16.020 |
| `PLAYER2` | 0 | -1.210 | 11.107 | 18.265 |
| `CSPLAY2` | 109 | 1.684 | 9.417 | 4.843 |

These built-in values are available on fresh installs even before a profile file
exists. If `gamedata/vr_cutscene_camera_anchors.cfg` is already present, its saved
entries override the built-in values for matching models; calibration changes
made in-headset continue to save to that file.

## In-headset controls

1. Enter a first-person stereo cutscene in which the target actor is visible.
2. Press **L3 + R3 together** to open the anchor calibration panel for the model
   currently driving the camera. If the current view is the director camera and
   no animated actor head is active, calibration cannot start; use R3 to select
   an actor camera first.
3. Move the anchor live with the sticks:
   - **Left stick X:** side-to-side.
   - **Left stick Y:** up/down.
   - **Right stick Y:** forward/back.
4. Press **A** to save and exit, or **B** / **L3 + R3** to discard changes.

Offsets are adjusted at a slow, rate-limited speed and are clamped to 50 cm in
each direction. Height is relative to the existing 4 cm lift. Saved profiles are
written to `gamedata/vr_cutscene_camera_anchors.cfg` in the Quest app's game-data
folder, so no manual `vr_settings.ini` edit or restart is needed.

The profile file has one record per model:

`MODEL_NAME modelIndex sideCm forwardCm heightCm`

Unrecognized model names reuse the appropriate existing PLAYER/CSPLAY profile;
recognized profiles such as `PLAYER2` and `CSPLAY2` continue to use their own
calibrations. This avoids making every future replacement start with zero offsets.
