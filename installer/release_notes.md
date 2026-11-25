<p align="center">
<picture>
<source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/dmajorsound/olAAF/main/installer/mac_ui/logo-dark.png">
<img src="https://raw.githubusercontent.com/dmajorsound/olAAF/main/installer/mac_ui/logo.png" alt="dmajorsound" width="200">
</picture>
</p>

<h2 align="center">Alfred</h2>
<p align="center"><i>A simple Denoiser at your Service</i></p>

Alfred splits a recording into Signal, Noise and Reverb and lets you rebalance them, for dialogue and sound effects.

### Download
- **Mac** (Apple Silicon and Intel): `Alfred_FILEVER_installer_mac.pkg` below (VST3, AU, CLAP)
- **Windows** (64-bit): `Alfred_FILEVER_installer_win64.exe` below (VST3, CLAP)

### Install
1. Quit REAPER.
2. Open the installer and click through. It puts Alfred into the common plug-in folders.
3. Start REAPER. If Alfred does not show up: **Options → Preferences → Plug-ins → VST** (or CLAP) **→ Re-scan**.

> [!CAUTION]
> **Mac: "Alfred can't be opened because it is from an unidentified developer"**
>
> The installer is not signed by Apple. Do **not** double-click it again. Instead:
> 1. **Right-click** (or Control-click) the .pkg and choose **Open**.
> 2. Click **Open** in the warning.
>
> Still blocked? Open **System Settings → Privacy & Security**, scroll down and click **Open Anyway** next to Alfred.

> [!CAUTION]
> **Windows: "Windows protected your PC" (SmartScreen)**
>
> The installer is not signed by Microsoft.
> 1. Click **More info**.
> 2. Click **Run anyway**.
>
> If your browser blocks the download, choose **Keep** in the download list.

### Enhance: Restore and Sidon (since 0.37)
The **ENHANCE** button next to NEURAL chooses how the Enhance fader works: Classic, **Restore** or **Sidon**. The two new ones are networks that bring a voice closer to a studio recording. They need several seconds of audio at once and are **not real time**:
- **Listening (since 0.39):** play a passage once. You hear it without the network while Alfred prepares it in the background (a red line at the bottom tells you). Play the same passage again and you hear the restored voice. Looping a passage works too.
- **Rendering** (in REAPER: right-click the item, *Apply track/take FX to items as new take*, or Render) always includes it.

With one of them chosen, Alfred's delay is 5 seconds longer; REAPER compensates for it. 48 kHz, Voice mode.
- **Restore** keeps the timing and character of the voice; the Enhance fader blends it in.
- **Sidon** builds the voice anew. It is slow (about real time) and is either on or off.

> [!NOTE]
> Restore is under the OpenRAIL license. Whoever uses it, or audio made with it, agrees to its use restrictions: no unlawful, harmful, harassing or discriminating use. Both installers show the license and ask you to accept it before Restore is installed (since 0.38); the text is also installed next to the model (`restore-LICENSE-OpenRAIL.txt`). Sidon is under the MIT License.

### Updates
Alfred looks at this page when you open its window. If a newer version is here, an **UPDATE** button appears at the bottom of the plug-in; a click opens the download page. Nothing is installed by itself, and no data other than the request to GitHub is sent.

V VERSION, developed with AI assistance · [dmajorsound.com](https://dmajorsound.com)
