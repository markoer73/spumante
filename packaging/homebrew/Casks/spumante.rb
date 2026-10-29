# SPDX-License-Identifier: Apache-2.0
# Homebrew Cask for Spumante for macOS
# Usage (from tap):
#   brew install --cask markoer73/spumante/spumante

cask "spumante" do
  version "1.0.0"
  sha256 "14b78954071f8a57f6c1a82ca22c20119a7559875eaabb5019434848852e77a5"

  url "https://github.com/markoer73/Spumante/releases/download/v#{version}/Spumante-Installer.dmg"
  name "Spumante"
  desc "Run Windows .exe and .msi apps on macOS in a zero-trust Seatbelt sandbox"
  homepage "https://github.com/markoer73/Spumante"

  depends_on macos: ">= :big_sur"

  app "Spumante.app"
  app "Spumante Config.app"
  binary "#{appdir}/Spumante.app/Contents/MacOS/Spumante", target: "spumante"
  binary "#{appdir}/Spumante Config.app/Contents/MacOS/SpumanteConfig", target: "spumante-config"

  postflight do
    system_command "#{appdir}/Spumante.app/Contents/MacOS/Spumante",
                   args: ["--register"],
                   must_succeed: false
    system_command "#{appdir}/Spumante Config.app/Contents/MacOS/SpumanteConfig",
                   args: ["--config-status"],
                   must_succeed: false
  end

  uninstall_preflight do
    if File.executable?("#{appdir}/Spumante.app/Contents/MacOS/Spumante")
      system_command "#{appdir}/Spumante.app/Contents/MacOS/Spumante",
                     args: ["--unregister"],
                     must_succeed: false
    end
  end

  zap trash: [
    "~/.config/spumante",
    "~/.spumante",
    "~/Library/LaunchAgents/org.spumante.tray.plist",
  ]

  caveats <<~EOS
    Spumante.app and Spumante Config.app have been installed into /Applications,
    along with the `spumante` and `spumante-config` CLI commands.

    On Apple Silicon Macs (M1/M2/M3/M4), running 32-bit and 64-bit Intel (x86/x86_64)
    Windows applications requires Apple Rosetta 2:
      softwareupdate --install-rosetta --agree-to-license

    For security, external downloaded .exe and .msi files must have the executable
    bit set (`chmod +x /path/to/app.exe`) before launching.
  EOS
end
