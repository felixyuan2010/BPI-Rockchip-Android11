#!/system/bin/sh
# Reconcile the Device Owner and product defaults from the booted system.
# UID 0 uses Android 11's provisioning command in the dedicated init domain.

PKG=com.jsy.deviceagent
ADMIN=${PKG}/.admin.AdminReceiver
OWNER=${PKG}/${PKG}.admin.AdminReceiver

report() {
    log -t deviceagent_do "$*"
}

if ! policy=$(dumpsys -t 5 device_policy 2>/dev/null); then
    report "cannot query Device Policy Manager"
    exit 1
fi
if ! printf '%s\n' "$policy" | grep -q '^[[:space:]]*Current Device Policy Manager state:'; then
    report "unexpected Device Policy Manager output"
    exit 1
fi
owner=$(printf '%s\n' "$policy" | sed -n '/^[[:space:]]*Device Owner:[[:space:]]*$/ {
    n
    s/^[[:space:]]*admin=ComponentInfo{\([^}]*\)}[[:space:]]*$/\1/p
    q
}')
if [ -n "$owner" ]; then
    if [ "$owner" != "$OWNER" ]; then
        report "device owner taken by another component"
        exit 1
    fi
    report "DeviceAgent already owns the device"
else
    if printf '%s\n' "$policy" | grep -q '^[[:space:]]*Device Owner:'; then
        report "cannot identify existing device owner"
        exit 1
    fi

    # Wait for PackageManager to expose the product package.
    attempts=0
    until pm path "$PKG" >/dev/null 2>&1; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 10 ]; then
            report "DeviceAgent not installed"
            exit 1
        fi
        sleep 1
    done

    dpm set-device-owner "$ADMIN"
    rc=$?
    if [ "$rc" -ne 0 ]; then
        report "set-device-owner failed rc=$rc"
        exit "$rc"
    fi
    report "DeviceAgent is device owner"
fi

# Restore appops after both clean flashes and preserve-data updates.
rc=0
for op in WRITE_SETTINGS SYSTEM_ALERT_WINDOW; do
    if ! cmd appops set com.jsy.visual "$op" allow; then
        report "failed to grant vision_app $op"
        rc=1
    fi
done

# Preserve any timeout other than the factory framework default of 60 seconds.
if [ "$(settings get system screen_off_timeout 2>/dev/null)" = "60000" ]; then
    if settings put system screen_off_timeout 900000; then
        report "screen timeout default 60s -> 15min"
    else
        report "failed to set product screen timeout"
        rc=1
    fi
fi
exit "$rc"
