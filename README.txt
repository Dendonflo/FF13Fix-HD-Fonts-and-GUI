================================================================================
                    FF XIII HD FONT & GUI MOD
                    built on FF13Fix
================================================================================

Replaces the original Final Fantasy XIII fonts, GUI elements, and map tiles
with high-resolution versions, resulting in sharper, cleaner visuals across
all menus, UI, and in-game maps.

This package includes a modified version of FF13Fix with the HD texture
system built in. It replaces FF13Fix — do NOT install both.

--------------------------------------------------------------------------------
INSTALLATION
--------------------------------------------------------------------------------

  Copy everything into:
  <game_install_folder>/white_data/prog/win/bin/

    d3d9.dll
    FF13Fix.ini
    hash_database.txt
    hd_textures/  (entire folder)

  Overwrite if prompted.

--------------------------------------------------------------------------------
UNINSTALLATION
--------------------------------------------------------------------------------

  Option A — Remove HD textures only:
    Delete hash_database.txt and the hd_textures/ folder.
    The d3d9.dll will continue to work as a standalone FF13Fix replacement.

  Option B — Full removal:
    Delete d3d9.dll, hash_database.txt, and hd_textures/.
    Reinstall the original FF13Fix if desired.

--------------------------------------------------------------------------------
COMPATIBILITY
--------------------------------------------------------------------------------

  DXVK
    Rename DXVK's x86 d3d9.dll to dxvk.dll. Keep this package's d3d9.dll.

  ReShade (Direct3D9)
    Install ReShade first, rename ReShade's d3d9.dll to ReShade32.dll,
    then install this package normally.

  ReShade (DXVK)
    Install ReShade targeting Vulkan and enable it globally.

  4GB Large Address Aware patch (Recommended)
    Patching the executable allows the game to use more than 2GB of RAM,
    reducing crashes especially at high resolutions or with mods.
    FF13:   Copy unpatched ffxiiiimg.exe to the bin folder as untouched.exe,
            then patch the original (https://ntcore.com/?page_id=371)
    FF13-2: Patch ffxiii2img.exe directly.

--------------------------------------------------------------------------------
CREDITS
--------------------------------------------------------------------------------

  HD Font & GUI Mod Dendonflo
  FF13Fix           PureDark, RaiderB and contributors
                    https://github.com/rebtd7/FF13Fix

================================================================================
                    FF13Fix — ORIGINAL README
================================================================================

Performance and bug fixes for the PC versions of FF13 and FF13-2.
This is a fork of OneTweakNG (https://github.com/Nucleoprotein/OneTweakNG)
with additional fixes for FF13. Thanks Nucleoprotein for starting this!

WHAT FF13FIX DOES
-----------------

  Removes the frame pacer
    Disabling this greatly improves the frame rate in certain situations.

  Removes stuttering from controller scanning
    The original game scanned for new controllers every second, causing
    stuttering especially without a controller connected through Steam.
    Note: this removes hotplugging support — connect controllers before
    launching the game.

  Enables controller vibration
    Can be toggled in FF13Fix.ini.

  Uncaps the frame rate (optional)
    Note: higher frame rates can cause jankiness on facial animations
    during in-game cutscenes. See https://github.com/rebtd7/FF13Fix/issues/3

  Enables Triple Buffering
    May make the frame rate more consistent.

  Uses desktop refresh rate in full screen mode
    The game originally forced 60Hz in full screen regardless of your
    monitor's actual refresh rate.

  Improves vertex buffer memory allocation
    Considerably improves frame rate when 2D elements are toggled on screen
    (minimap, battle HUD, etc.). Originally from OneTweakNG.

  Fixes misaligned screen space effects
    Fixes rendering issues at 1440p and above.

  Fixes enemy scan text above 720p (FF13 only)
    The game used hardcoded 720p coordinates for the scan text scissor rect.
    This corrects the rectangle to match the actual resolution.

OTHER NOTES
-----------

  * Not compatible with GeDoSaTo.
  * Forcing anisotropic filtering in your GPU driver is recommended
    for improved texture quality.
  * Setting power management to "Maximum Performance" in your GPU driver
    can help keep the frame rate stable.

  Reporting issues:
    Specify which game, which mods (DXVK?), your system specs, and attach
    FF13Fix.log. A save file and reproduction steps are helpful when possible.

================================================================================
