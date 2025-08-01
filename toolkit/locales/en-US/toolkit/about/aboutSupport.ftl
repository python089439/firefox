# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

page-title = Troubleshooting Information
page-subtitle =
    This page contains technical information that might be useful when you’re
    trying to solve a problem. If you are looking for answers to common questions
    about { -brand-short-name }, check out our <a data-l10n-name="support-link">support website</a>.

crashes-title = Crash Reports
crashes-id = Report ID
crashes-send-date = Submitted
crashes-all-reports = All Crash Reports
crashes-no-config = This application has not been configured to display crash reports.
support-addons-title = Add-ons
support-addons-name = Name
support-addons-type = Type
support-addons-enabled = Enabled
support-addons-version = Version
support-addons-id = ID
# In the add-on world, locations are where the addon files are stored. Each
# location has name. For instance: app-system-addons, app-builtin,
# app-temporary, etc.
support-addons-location-name = Location
legacy-user-stylesheets-title = Legacy User Stylesheets
legacy-user-stylesheets-enabled = Active
legacy-user-stylesheets-stylesheet-types = Stylesheets
legacy-user-stylesheets-no-stylesheets-found = No stylesheets found
security-software-title = Security Software
security-software-type = Type
security-software-name = Name
security-software-antivirus = Antivirus
security-software-antispyware = Antispyware
security-software-firewall = Firewall
processes-title = Remote Processes
processes-type = Type
processes-count = Count
app-basics-title = Application Basics
app-basics-name = Name
app-basics-version = Version
app-basics-build-id = Build ID
app-basics-distribution-id = Distribution ID
app-basics-update-channel = Update Channel
# This message refers to the folder used to store updates on the device,
# as in "Folder for updates". "Update" is a noun, not a verb.
app-basics-update-dir =
    { PLATFORM() ->
        [linux] Update Directory
       *[other] Update Folder
    }
app-basics-update-history = Update History
app-basics-show-update-history = Show Update History
# Represents the path to the binary used to start the application.
app-basics-binary = Application Binary
app-basics-profile-dir =
    { PLATFORM() ->
        [linux] Profile Directory
       *[other] Profile Folder
    }
app-basics-build-config = Build Configuration
app-basics-user-agent = User Agent
app-basics-os = OS
app-basics-os-theme = OS Theme
# Rosetta is Apple's translation process to run apps containing x86_64
# instructions on Apple Silicon. This should remain in English.
app-basics-rosetta = Rosetta Translated
app-basics-memory-use = Memory Use
app-basics-performance = Performance
app-basics-service-workers = Registered Service Workers
app-basics-third-party = Third-party Modules
app-basics-profiles = Profiles
app-basics-launcher-process-status = Launcher Process
app-basics-multi-process-support = Multiprocess Windows
app-basics-fission-support = Fission Windows
app-basics-remote-processes-count = Remote Processes
app-basics-enterprise-policies = Enterprise Policies
app-basics-location-service-key-google = Google Location Service Key
app-basics-safebrowsing-key-google = Google Safebrowsing Key
app-basics-key-mozilla = Mozilla Location Service Key
app-basics-safe-mode = Safe Mode
app-basics-memory-size = Memory Size (RAM)
app-basics-disk-available = Disk Space Available
app-basics-pointing-devices = Pointing Devices

# Variables:
#   $value (number) - Amount of data being stored
#   $unit (string) - The unit of data being stored (e.g. MB)
app-basics-data-size = { $value } { $unit }

show-dir-label =
    { PLATFORM() ->
        [macos] Show in Finder
        [windows] Open Folder
       *[other] Open Directory
    }
environment-variables-title = Environment Variables
environment-variables-name = Name
environment-variables-value = Value
modified-key-prefs-title = Important Modified Preferences
modified-prefs-name = Name
modified-prefs-value = Value
user-js-title = user.js Preferences
user-js-description = Your profile folder contains a <a data-l10n-name="user-js-link">user.js file</a>, which includes preferences that were not created by { -brand-short-name }.
locked-key-prefs-title = Important Locked Preferences
locked-prefs-name = Name
locked-prefs-value = Value
graphics-title = Graphics
graphics-features-title = Features
graphics-diagnostics-title = Diagnostics
graphics-failure-log-title = Failure Log
graphics-gpu1-title = GPU #1
graphics-gpu2-title = GPU #2
graphics-decision-log-title = Decision Log
graphics-crash-guards-title = Crash Guard Disabled Features
graphics-workarounds-title = Workarounds
graphics-device-pixel-ratios = Window Device Pixel Ratios
# Windowing system in use on Linux (e.g. X11, Wayland).
graphics-window-protocol = Window Protocol
# Desktop environment in use on Linux (e.g. GNOME, KDE, XFCE, etc).
graphics-desktop-environment = Desktop Environment
place-database-title = Places Database
place-database-stats = Statistics
place-database-stats-show = Show Statistics
place-database-stats-hide = Hide Statistics
place-database-stats-entity = Entity
place-database-stats-count = Count
place-database-stats-size-kib = Size (KiB)
place-database-stats-size-perc = Size (%)
place-database-stats-efficiency-perc = Efficiency (%)
place-database-stats-sequentiality-perc = Sequentiality (%)
place-database-integrity = Integrity
place-database-verify-integrity = Verify Integrity
a11y-title = Accessibility
a11y-activated = Activated
a11y-force-disabled = Prevent Accessibility
a11y-handler-used = Accessible Handler Used
a11y-instantiator = Accessibility Instantiator
library-version-title = Library Versions
copy-text-to-clipboard-label = Copy text to clipboard
copy-raw-data-to-clipboard-label = Copy raw data to clipboard
sandbox-title = Sandbox
sandbox-sys-call-log-title = Rejected System Calls
sandbox-sys-call-index = #
sandbox-sys-call-age = Seconds Ago
sandbox-sys-call-pid = PID
sandbox-sys-call-tid = TID
sandbox-sys-call-proc-type = Process Type
sandbox-sys-call-number = Syscall
sandbox-sys-call-args = Arguments
troubleshoot-mode-title = Diagnose issues
restart-in-troubleshoot-mode-label = Troubleshoot Mode…
clear-startup-cache-title = Try clearing the startup cache
clear-startup-cache-label = Clear startup cache…
startup-cache-dialog-title2 = Restart { -brand-short-name } to clear startup cache?
startup-cache-dialog-body2 = This will not change your settings or remove extensions.
restart-button-label = Restart

## Media titles

audio-backend = Audio Backend
max-audio-channels = Max Channels
sample-rate = Preferred Sample Rate
roundtrip-latency = Roundtrip latency (standard deviation)
media-title = Media
media-output-devices-title = Output Devices
media-input-devices-title = Input Devices
media-device-name = Name
media-device-group = Group
media-device-vendor = Vendor
media-device-state = State
media-device-preferred = Preferred
media-device-format = Format
media-device-channels = Channels
media-device-rate = Rate
media-device-latency = Latency
media-capabilities-title = Media Capabilities
media-codec-support-info = Codec Support Information
# List all the entries of the database.
media-capabilities-enumerate = Enumerate database

## Codec support table

media-codec-support-sw-decoding = Software Decoding
media-codec-support-hw-decoding = Hardware Decoding
media-codec-support-sw-encoding = Software Encoding
media-codec-support-hw-encoding = Hardware Encoding
media-codec-support-codec-name = Codec Name
media-codec-support-supported = Supported
media-codec-support-unsupported = Unsupported
media-codec-support-error = Codec support information unavailable. Try again after playing back a media file.
media-codec-support-lack-of-extension = Install extension

## Media Content Decryption Modules (CDM)
## See EME Spec for more explanation for following technical terms
## https://w3c.github.io/encrypted-media/

media-content-decryption-modules-title = Content Decryption Modules Information
media-key-system-name = Key System Name
media-video-robustness = Video Robustness
media-audio-robustness = Audio Robustness
media-cdm-capabilities = Capabilities
# Clear Lead isn't defined in the spec, which means the the first few seconds
# are not encrypted. It allows playback to start without having to wait for
# license response, improving video start time and user experience.
media-cdm-clear-lead = Clear Lead
# We choose 2.2 as this is the version which the video provider usually want to have in order to stream 4K video securely
# HDCP version https://w3c.github.io/encrypted-media/#idl-def-hdcpversion
media-hdcp-22-compatible = HDCP 2.2 Compatible

##

intl-title = Internationalization & Localization
intl-app-title = Application Settings
intl-locales-requested = Requested Locales
intl-locales-available = Available Locales
intl-locales-supported = App Locales
intl-locales-default = Default Locale
intl-os-title = Operating System
intl-os-prefs-system-locales = System Locales
intl-regional-prefs = Regional Preferences

## Remote Debugging
##
## The Firefox remote protocol provides low-level debugging interfaces
## used to inspect state and control execution of documents,
## browser instrumentation, user interaction simulation,
## and for subscribing to browser-internal events.
##
## See also https://firefox-source-docs.mozilla.org/remote/

remote-debugging-title = Remote Debugging (Chromium Protocol)
remote-debugging-accepting-connections = Accepting Connections
remote-debugging-url = URL

##

# Variables
# $days (Integer) - Number of days of crashes to log
report-crash-for-days =
    { $days ->
        [one] Crash Reports for the Last { $days } Day
       *[other] Crash Reports for the Last { $days } Days
    }

# Variables
# $minutes (integer) - Number of minutes since crash
crashes-time-minutes =
    { $minutes ->
        [one] { $minutes } minute ago
       *[other] { $minutes } minutes ago
    }

# Variables
# $hours (integer) - Number of hours since crash
crashes-time-hours =
    { $hours ->
        [one] { $hours } hour ago
       *[other] { $hours } hours ago
    }

# Variables
# $days (integer) - Number of days since crash
crashes-time-days =
    { $days ->
        [one] { $days } day ago
       *[other] { $days } days ago
    }

# Variables
# $reports (integer) - Number of pending reports
pending-reports =
    { $reports ->
        [one] All Crash Reports (including { $reports } pending crash in the given time range)
       *[other] All Crash Reports (including { $reports } pending crashes in the given time range)
    }

raw-data-copied = Raw data copied to clipboard
text-copied = Text copied to clipboard

## The verb "blocked" here refers to a graphics feature such as "Direct2D" or "OpenGL layers".

blocked-driver = Blocked for your graphics driver version.
blocked-gfx-card = Blocked for your graphics card because of unresolved driver issues.
blocked-os-version = Blocked for your operating system version.
blocked-mismatched-version = Blocked for your graphics driver version mismatch between registry and DLL.
# Variables
# $driverVersion - The graphics driver version string
try-newer-driver = Blocked for your graphics driver version. Try updating your graphics driver to version { $driverVersion } or newer.

# "ClearType" is a proper noun and should not be translated. Feel free to leave English strings if
# there are no good translations, these are only used in about:support
clear-type-parameters = ClearType Parameters

compositing = Compositing
support-font-determination = Font Visibility Debug Info
hardware-h264 = Hardware H264 Decoding
main-thread-no-omtc = main thread, no OMTC
yes = Yes
no = No
unknown = Unknown
virtual-monitor-disp = Virtual Monitor Display

## The following strings indicate if an API key has been found.
## In some development versions, it's expected for some API keys that they are
## not found.

found = Found
missing = Missing

gpu-process-pid = GPUProcessPid
gpu-process = GPUProcess
gpu-description = Description
gpu-vendor-id = Vendor ID
gpu-device-id = Device ID
gpu-subsys-id = Subsys ID
gpu-drivers = Drivers
gpu-ram = RAM
gpu-driver-vendor = Driver Vendor
gpu-driver-version = Driver Version
gpu-driver-date = Driver Date
gpu-active = Active
webgl1-wsiinfo = WebGL 1 Driver WSI Info
webgl1-renderer = WebGL 1 Driver Renderer
webgl1-version = WebGL 1 Driver Version
webgl1-driver-extensions = WebGL 1 Driver Extensions
webgl1-extensions = WebGL 1 Extensions
webgl2-wsiinfo = WebGL 2 Driver WSI Info
webgl2-renderer = WebGL 2 Driver Renderer
webgl2-version = WebGL 2 Driver Version
webgl2-driver-extensions = WebGL 2 Driver Extensions
webgl2-extensions = WebGL 2 Extensions
webgpu-default-adapter = WebGPU Default Adapter
webgpu-fallback-adapter = WebGPU Fallback Adapter

# Variables
#   $bugNumber (string) - Bug number on Bugzilla
support-blocklisted-bug = Blocklisted due to known issues: <a data-l10n-name="bug-link">bug { $bugNumber }</a>

# Variables
# $failureCode (string) - String that can be searched in the source tree.
unknown-failure = Blocklisted; failure code { $failureCode }

d3d11layers-crash-guard = D3D11 Compositor
glcontext-crash-guard = OpenGL
wmfvpxvideo-crash-guard = WMF VPX Video Decoder

reset-on-next-restart = Reset on Next Restart
gpu-process-kill-button = Terminate GPU Process
gpu-device-reset = Device Reset
gpu-device-reset-button = Trigger Device Reset
uses-tiling = Uses Tiling
content-uses-tiling = Uses Tiling (Content)
off-main-thread-paint-enabled = Off Main Thread Painting Enabled
off-main-thread-paint-worker-count = Off Main Thread Painting Worker Count
target-frame-rate = Target Frame Rate

min-lib-versions = Expected minimum version
loaded-lib-versions = Version in use

has-seccomp-bpf = Seccomp-BPF (System Call Filtering)
has-seccomp-tsync = Seccomp Thread Synchronization
has-user-namespaces = User Namespaces
has-privileged-user-namespaces = User Namespaces for privileged processes
# Variables
# $status (string) - Boolean value of hasUserNamespaces (should only be false when support-user-namespaces-unavailable is used)
support-user-namespaces-unavailable =
    { $status } — This feature is not allowed by your system. This can restrict security features of { -brand-short-name }.
can-sandbox-content = Content Process Sandboxing
can-sandbox-media = Media Plugin Sandboxing
content-sandbox-level = Content Process Sandbox Level
effective-content-sandbox-level = Effective Content Process Sandbox Level
content-win32k-lockdown-state = Win32k Lockdown State for Content Process
support-sandbox-gpu-level = GPU Process Sandbox Level
sandbox-proc-type-content = content
sandbox-proc-type-file = file content
sandbox-proc-type-media-plugin = media plugin
sandbox-proc-type-data-decoder = data decoder

startup-cache-title = Startup Cache
startup-cache-disk-cache-path = Disk Cache Path
startup-cache-ignore-disk-cache = Ignore Disk Cache
startup-cache-found-disk-cache-on-init = Found Disk Cache on Init
startup-cache-wrote-to-disk-cache = Wrote to Disk Cache

launcher-process-status-0 = Enabled
launcher-process-status-1 = Disabled due to failure
launcher-process-status-2 = Disabled forcibly
launcher-process-status-unknown = Unknown status

# Variables
# $remoteWindows (integer) - Number of remote windows
# $totalWindows (integer) - Number of total windows
multi-process-windows = { $remoteWindows }/{ $totalWindows }
# Variables
# $fissionWindows (integer) - Number of remote windows
# $totalWindows (integer) - Number of total windows
fission-windows = { $fissionWindows }/{ $totalWindows }
fission-status-disabled-by-e10s-env = Disabled by environment
fission-status-enabled-by-env = Enabled by environment
fission-status-disabled-by-env = Disabled by environment
fission-status-enabled-by-default = Enabled by default
fission-status-disabled-by-default = Disabled by default
fission-status-enabled-by-user-pref = Enabled by user
fission-status-disabled-by-user-pref = Disabled by user
fission-status-disabled-by-e10s-other = E10s disabled

async-pan-zoom = Asynchronous Pan/Zoom
apz-none = none
wheel-enabled = wheel input enabled
touch-enabled = touch input enabled
drag-enabled = scrollbar drag enabled
keyboard-enabled = keyboard enabled
autoscroll-enabled = autoscroll enabled
zooming-enabled = smooth pinch-zoom enabled

## Variables
## $preferenceKey (string) - String ID of preference

wheel-warning = async wheel input disabled due to unsupported pref: { $preferenceKey }
touch-warning = async touch input disabled due to unsupported pref: { $preferenceKey }

## Strings representing the status of the Enterprise Policies engine.

policies-inactive = Inactive
policies-active = Active
policies-error = Error

## Printing section

support-printing-title = Printing
support-printing-troubleshoot = Troubleshooting
support-printing-clear-settings-button = Clear saved print settings
support-printing-modified-settings = Modified print settings
support-printing-prefs-name = Name
support-printing-prefs-value = Value

## Remote Settings sections

support-remote-settings-title = Remote Settings
support-remote-settings-status = Status
support-remote-settings-status-ok = OK
# Status when synchronization is not working.
support-remote-settings-status-broken = Not working
support-remote-settings-last-check = Last check
support-remote-settings-local-timestamp = Local timestamp
support-remote-settings-sync-history = History
support-remote-settings-sync-history-status = Status
support-remote-settings-sync-history-datetime = Date
support-remote-settings-sync-history-infos = Infos

## Normandy sections

support-remote-experiments-title = Remote Experiments
support-remote-experiments-name = Name
support-remote-experiments-branch = Experiment Branch
support-remote-experiments-see-about-studies = See <a data-l10n-name="support-about-studies-link">about:studies</a> for more information, including how to disable individual experiments or to disable { -brand-short-name } from running this type of experiment in the future.

support-remote-features-title = Remote Features
support-remote-features-name = Name
support-remote-features-status = Status

## Pointing devices

pointing-device-mouse = Mouse
pointing-device-touchscreen = Touchscreen
pointing-device-pen-digitizer = Pen Digitizer
pointing-device-none = No pointing devices

## Content Analysis (DLP)

# DLP stands for Data Loss Prevention, an industry term for external software
# that enterprises can set up to prevent sensitive data from being transferred
# to external websites.
content-analysis-title = Content Analysis (DLP)
content-analysis-active = Active
content-analysis-connected-to-agent = Connected to Agent
content-analysis-agent-path = Agent Path
content-analysis-agent-failed-signature-verification = Agent Failed Signature Verification
content-analysis-request-count = Request Count
