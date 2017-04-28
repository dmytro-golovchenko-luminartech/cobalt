# Starboard Version Changelog

This document records all changes made to the Starboard interface, up to the
current version, but not including the experimental version.  This file will
be updated each time a new Starboard version is released.  Each section in
this file describes the changes made to the Starboard interface since the
version previous to it.

## Version 4

### Decode-to-Texture Player Output Mode
Feature introducing support for decode-to-texture player output mode, and
runtime player output mode selection and detection.
In `starboard/configuration.h`,
  * `SB_IS_PLAYER_PUNCHED_OUT`, `SB_IS_PLAYER_PRODUCING_TEXTURE`, and
    `SB_IS_PLAYER_COMPOSITED` now no longer need to be defined (and should not
    be defined) by platforms.  Instead, these capabilities are detected at
    runtime via `SbPlayerOutputModeSupported()`.
In `starboard/player.h`,
  * The enum `SbPlayerOutputMode` is introduced.
  * `SbPlayerOutputModeSupported()` is introduced to let applications query
    for player output mode support.
  * `SbPlayerCreate()` now takes an additional parameter that specifies the
    desired output mode.
  * The punch out specific function `SbPlayerSetBounds()` must now be
    defined on all platforms, even if they don't support punch out (in which
    case they can implement a stub).
  * The function `SbPlayerGetCompositionHandle()` is removed.
  * The function `SbPlayerGetTextureId()` is replaced by the new
    `SbPlayerGetCurrentFrame()`, which returns a `SbDecodeTarget`.
In `starboard/decode_target.h`,
  * All get methods (`SbDecodeTargetGetPlane()` and `SbDecodeTargetGetFormat()`,
    `SbDecodeTargetIsOpaque()`) are now replaced with `SbDecodeTargetGetInfo()`.
  * The `SbDecodeTargetInfo` structure is introduced and is the return value
    type of `SbDecodeTargetGetInfo()`.
  * `SbDecdodeTargetCreate()` is now responsible for creating all its internal
    planes, and so its `planes` parameter is replaced by `width` and
    `height` parameters.
  * The GLES2 version of `SbDecdodeTargetCreate()` has its EGL types
    (`EGLDisplay`, `EGLContext`) replaced by `void*` types, so that
    `decode_target.h` can avoid #including EGL/GLES2 headers.
  * `SbDecodeTargetDestroy()` is renamed to `SbDecodeTargetRelease()`.
In `starboard/player.h`, `starboard/image.h` and `starboard/decode_target.h`,
  * Replace `SbDecodeTargetProvider` with
    `SbDecodeTargetGraphicsContextProvider`.
  * Instead of restricting Starboard implementations to only be able to run
    `SbDecodeTarget` creation and destruction code on the application's
    renderer's thread with the application's renderer's `EGLContext` current,
    Starboard implementations can now run arbitrary code on the application's
    renderer's thread with its `EGLContext` current.
  * Remove `SbDecodeTargetCreate()`, `SbDecodeTarget` creation is now an
    implementation detail to be dealt with in other Starboard API functions
    that create `SbDecodeTargets`, like `SbImageDecode()` or `SbPlayerCreate()`.

### Playback Rate
Support for setting the playback rate on an SbPlayer.  This allows for control
of the playback speed of video at runtime.

### Floating Point Input Vector
Change `input.h`'s `SbInputVector` structure to contain float members instead of
ints.

### Delete SbUserApplicationTokenResults
Deleted the vestigal struct `SbUserApplicationTokenResults` from `user.h`.

### Storage Options for Encoded Audio/Video Data
Enables the SbPlayer implementation to provide instructions to its user on
how to store audio/video data.  Encoded audio/video data is cached once being
demuxed and may occupy a significant amount of memory.  Enabling this feature
allows the SbPlayer implementation to have better control on where encoded
audio/video data is stored.

### Unified implementation of `SbMediaCanPlayMimeAndKeySystem()`
Use a unified implementation of `SbMediaCanPlayMimeAndKeySystem()` based on
`SbMediaIsSupported()`, `SbMediaIsAudioSupported()`, and
`SbMediaIsVideoSupported()`.

### Introduce `ticket` parameter to `SbDrmGenerateSessionUpdateRequest()`
Introduce `ticket` parameter to `SbDrmGenerateSessionUpdateRequest()`
and `SbDrmSessionUpdateRequestFunc` to allow distinguishing between callbacks
from multiple concurrent calls.

### Introduce `SbSocketGetInterfaceAddress()`
`SbSocketGetInterfaceAddress()` is introduced to let applications find out
which source IP address and the associated netmask will be used to connect to
the destination. This is very important for multi-homed devices, and for
certain conditions in IPv6.

### Introduce `starboard/cryptography.h`
In newly-introduced `starboard/cryptography.h`,
  * Optional support for accelerated cryptography, which can, in
    particular, be used for accelerating SSL.

### Introduce z-index parameter to `SbPlayerSetBounds()`
Allow `SbPlayerSetBounds` to use an extra parameter to indicate the z-index of
the video so multiple overlapping videos can be rendered.

### Media source buffer settings removed from `configuration.h`
Media source buffer settings in Starboard.

### Introduce `starboard/accessibility.h`
In particular, the functions `SbAccessibilityGetDisplaySettings()` and
`SbAccessibilityGetTextToSpeechSettings()` have now been introduced.

Additionally, a new Starboard event, `kSbEventTypeAccessiblitySettingsChanged`,
has been defined in `starboard/event.h`.

### HDR decode support
In `starboard/media.h`, `SbMediaColorMetadata` is now defined and it contains
HDR metadata. The field `SbMediaColorMetadata color_metadata` is now added to
`SbMediaVideoSampleInfo`.

### Add `kSbSystemDeviceTypeAndroidTV` to `starboard/system.h`
A new device type, `kSbSystemDeviceTypeAndroidTV`, is added to
starboard/system.h.

### Deprecate `SbSpeechSynthesisSetLanguage()`
SbSpeechSynthesisSetLanguage() has been deprecated.

### Request State Change Support
Added `SbSystemRequestPause()`, `SbSystemRequestUnpause()`,
`SbSystemRequestSuspend()`.

`SbSystemRequestSuspend()` in particular can be hooked into a platform's "hide"
or "minimize" window functionality.

### Font Directory Path Support
Added `kSbSystemPathFontDirectory` and `kSbSystemPathFontConfigurationDirectory`
which can be optionally specified for platforms that want to provide system
fonts to Starboard applications. The font and font configuration formats
supported are application-specific.

### Add `SB_NORETURN` to `starboard/configuration.h`.
Added attribute macro `SB_NORETURN` to allow functions to be marked as noreturn.

### Mark `SbSystemBreakIntoDebugger` `SB_NORETURN`.
Add `SB_NORETURN` to declaration of `SbSystemBreakIntoDebugger`, to allow it to
be used in a manner similar to `abort`.
