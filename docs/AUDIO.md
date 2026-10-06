# Sound

How Fleetwm gets sound working, what to check when it does not, and how other desktops divide the work.

## The layers

1. **Kernel driver and firmware** (`firmware-sof-signed` for Intel SOF boards) create the sound card.
2. **`alsa-ucm-conf`** describes the card's mixer: which controls route audio to the speakers, the headphones
   and the microphones (UCM = Use Case Manager). Without it WirePlumber falls back to a generic "stereo-fallback"
   profile that does not know the codec.
3. **`alsactl init`** applies the card's UCM `BootSequence` once (output mixers on, volumes set). The
   `alsa-restore` service does it at boot when there is no saved state, and the installer does it right after
   installing the packages and saves the result (`alsactl store`), so sound works without a reboot.
4. **PipeWire + WirePlumber** are the sound server and the session manager. WirePlumber picks the UCM profile
   and creates the sinks (Speakers, HDMI outputs) and sources (microphone).
5. **`pipewire-pulse` and `pipewire-alsa`** let programs that speak PulseAudio (browsers, players) or plain ALSA
   use PipeWire.
6. **Fleetwm** shows and sets the volume through PipeWire itself (the bar's readout and `fleetwm-audiomixer`).

## What other desktops do

They keep the ALSA mixer out of the GUI. `pavucontrol`, GNOME's volume control and KDE's `plasma-pa` talk to
the sound server over the PulseAudio protocol; `pwvucontrol` and Waybar's wireplumber module talk to PipeWire /
WirePlumber directly (`wpctl` is the command line for the same thing). All of them only set the volume, the mute
state and the default device of PipeWire nodes. Routing inside the codec belongs to UCM and `alsactl`, which the
distribution runs. Fleetwm's mixer follows the PipeWire-native approach, and the installer provides the system
part.

## A real case: Intel Gemini Lake with an ES8336 codec

Symptoms: PipeWire showed "Speakers", the volume bar moved, nothing came out. `amixer -c0 scontents` showed
`Headphone` at 0%, `Left/Right Headphone Mixer ... DAC` off and `DAC` at its default. The cause was that
`alsa-ucm-conf` had been installed after boot, so its `BootSequence` had never run. `sudo alsactl init`, then a
restart of the sound services (`systemctl --user restart wireplumber pipewire pipewire-pulse`), fixed it. (The
kernel log line `sof-essx8336: quirk mask 0x0` is normal for a board without a DMI quirk entry; the speaker
amplifier line showed as `speakers-enable ... ACTIVE LOW` and was asserted once audio played.)

## Checking sound without ears

Play a tone to the default sink and record it with the built-in microphone, then compare with a recording of
silence. On the test laptop the microphone read about 170 (rms) in a quiet room, about 200 with a tone that
the speakers did not play, and over 20000 once they did:

```
pw-record --rate 48000 --channels 1 /tmp/rec.wav &      # then
pw-play tone.wav                                         # a 1 kHz tone
```

Useful commands: `wpctl status` (sinks, sources, default marked `*`), `wpctl inspect @DEFAULT_AUDIO_SINK@`
(profile name, ALSA path), `amixer -c0 scontents`, `sudo cat /sys/kernel/debug/gpio` (speaker amplifier line),
`sudo dmesg | grep -i sof`.
