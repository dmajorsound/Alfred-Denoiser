#!/bin/bash
# Installs Alfred (VST3, AU, CLAP) for the current user and clears the download quarantine.
cd "$(dirname "$0")"
set -e
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components ~/Library/Audio/Plug-Ins/CLAP
rm -rf ~/Library/Audio/Plug-Ins/VST3/Alfred.vst3 ~/Library/Audio/Plug-Ins/Components/Alfred.component ~/Library/Audio/Plug-Ins/CLAP/Alfred.clap
cp -R Alfred.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R Alfred.component ~/Library/Audio/Plug-Ins/Components/
cp -R Alfred.clap ~/Library/Audio/Plug-Ins/CLAP/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Alfred.vst3 ~/Library/Audio/Plug-Ins/Components/Alfred.component ~/Library/Audio/Plug-Ins/CLAP/Alfred.clap 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo ""
echo "Alfred is installed. Restart REAPER (or rescan plug-ins) to find it."
