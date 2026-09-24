#!/usr/bin/env bash
# Host integration for the Crypto TKey S3 (run once, needs root):
#   * udev rule: Firefox (snap) + Chrome can use the key
#   * reaper-tkey-auth + patched concurrent biometric PAM module: sudo / Polkit / lock
#     screen / login accept Face ID, fingerprint OR a TKey tap, whichever comes first
# Register the key for PAM first (as your user): pamu2fcfg > ~/.config/Yubico/u2f_keys
set -euo pipefail
[ "$EUID" -eq 0 ] || exec sudo "$0" "$@"
cd "$(dirname "$0")"

command -v fido2-assert >/dev/null || apt-get install -y fido2-tools

install -m 0644 60-crypto-tkey-s3.rules /etc/udev/rules.d/60-crypto-tkey-s3.rules
install -m 0755 reaper-tkey-auth /usr/local/bin/reaper-tkey-auth

PAM_MOD=/lib/security/reaper_concurrent_biometric_pam.py
if [ -f reaper_concurrent_biometric_pam.py ] && ! cmp -s reaper_concurrent_biometric_pam.py "$PAM_MOD"; then
    cp -a "$PAM_MOD" "$PAM_MOD.bak-$(date +%Y%m%d-%H%M%S)"
    install -m 0644 reaper_concurrent_biometric_pam.py "$PAM_MOD"
    echo "PAM module updated (backup kept next to it)"
fi

udevadm control --reload
udevadm trigger --subsystem-match=hidraw --action=change
echo "✅ Done. Restart Firefox so its sandbox picks up the key."
