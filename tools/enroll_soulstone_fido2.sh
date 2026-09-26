#!/usr/bin/env bash
# enroll_soulstone_fido2.sh — Rough Draft: Enroll Crypto TKey S3 into Soul Stone LUKS2 keyslot
set -euo pipefail

LUKS_UUID="8636c4f3-09c8-42ce-bf9b-fe273be32b3f"
DEVICE_PATH="/dev/disk/by-uuid/${LUKS_UUID}"
BACKUP_DIR="${HOME}/.config/soulstone/backups"

echo "=== Soul Stone FIDO2 (TKey S3) Cryptenroll Suite ==="

# 1. Verify block device exists
if [ ! -b "${DEVICE_PATH}" ]; then
    # Fallback check sdb2
    if [ -b "/dev/sdb2" ]; then
        DEVICE_PATH="/dev/sdb2"
    else
        echo "[-] Error: Soul Stone LUKS device not found (${DEVICE_PATH})" >&2
        exit 1
    fi
fi
echo "[+] Target LUKS device found: ${DEVICE_PATH}"

# 2. Check FIDO2 token detection
FIDO_LIST=$(systemd-cryptenroll --fido2-device=list 2>/dev/null || true)
if ! echo "${FIDO_LIST}" | grep -qi "Espressif\|303a"; then
    echo "[-] Error: Crypto TKey S3 not detected by systemd-cryptenroll" >&2
    echo "${FIDO_LIST}"
    exit 1
fi
echo "[+] Crypto TKey S3 detected:"
echo "${FIDO_LIST}" | grep -i "Espressif\|303a"

# 3. Create LUKS header backup
mkdir -p "${BACKUP_DIR}"
BACKUP_FILE="${BACKUP_DIR}/luks_header_$(date +%Y%m%d_%H%M%S).img"
echo "[*] Creating safety backup of LUKS header..."
sudo cryptsetup luksHeaderBackup "${DEVICE_PATH}" --header-backup-file "${BACKUP_FILE}"
chmod 0600 "${BACKUP_FILE}"
echo "[+] Header backed up to: ${BACKUP_FILE}"

# 4. Enroll FIDO2 device (HMAC-Secret)
echo "[*] Ready to enroll TKey S3."
echo "[*] When prompted, enter your current Soul Stone passphrase, then TAP the TKey physical button when it flashes."

sudo systemd-cryptenroll "${DEVICE_PATH}" \
    --fido2-device=auto \
    --fido2-with-client-pin=no \
    --fido2-with-user-presence=yes

echo "[+] Successfully enrolled TKey S3 to ${DEVICE_PATH}!"
echo "[*] Verifying LUKS tokens..."
sudo cryptsetup luksDump "${DEVICE_PATH}" | grep -A 8 "systemd-fido2" || true
echo "=== Enrollment Complete ==="
