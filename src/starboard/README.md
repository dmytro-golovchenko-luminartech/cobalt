# Starboard

Starboard is Cobalt's porting layer and OS abstraction. It attempts to encompass
all the platform-specific functionality that Cobalt actually uses, and nothing
that it does not.


## Current State

Starboard is still in development, and now runs Cobalt. The biggest things that
are missing, but coming soon, are:

  * Media support. Some APIs have been defined, but they are not complete, they
    are not wired into Cobalt, they aren't yet implemented, and they are subject
    to change.
  * Blitter support. This is support for a hardware accelerated 2D blitter,
    which some older platforms will have instead of OpenGL.


## Interesting Source Locations

All source locations are specified relative to `src/starboard/` (this directory).

  * `.` - This is the root directory for the Starboard project, and contains all
    the public headers that Starboard defines.
  * `examples/` - Example code demonstrating various aspects of Starboard API
    usage.
  * `linux/` - The home of the Linux Starboard implementation. This contains a
    `starboard_platform.gyp` file that defines a library with all the source
    files needed to provide a complete Starboard Linux implementation. Source
    files that are specific to Linux are in this directory, whereas shared
    implementations are pulled from the shared directory.
  * `nplb/` - "No Platform Left Behind," Starboard's platform verification test
    suite.
  * `shared/` - The home of all code that can be shared between Starboard
    implementations. Subdirectories delimit code that can be shared between
    platforms that share some facet of their OS API.


## Building with Starboard

Follow the Cobalt instructions, except when invoking gyp:

    $ cobalt/build/gyp_cobalt -C debug linux-x64x11

and when invoking ninja:

    $ ninja -C out/linux-x64x11_debug cobalt


## Quick Guide to Starting a Port

### I. Enumerate and Name Your Platform Configurations

Before starting a Cobalt/Starboard port, first you will need to define the
canonical names for your set of platform configurations. These will be used when
organizing the code for your platforms.

What determines what goes into one platform configuration versus another? A
platform configuration has a one-to-one mapping to a production binary. So, if
you will need to produce a new binary, you are going to need a new platform
configuration for that.

The recommended naming convention for a `<platform-configuration>` is:

    <family-name>-<binary-variant>

Where `<family-name>` is a name specific to the family of products you are
porting to Starboard and `<binary-variant>` is one or more tokens that uniquely
describe the specifics of the binary you want that configuration to produce.

For example, let's say your company is named BobCo. BobCo employs multiple
different device architectures so it will need to define multiple platform
configurations.

All the BobCo devices are called BobBox, so it's a reasonable choice as a
product `<family-name>`. But they have both big- and little-endian MIPS
chips. So they might define two platform configurations:

  1. `bobbox-mipseb` - For big-endian MIPS devices.
  1. `bobbox-mipsel` - For little-endian MIPS devices.


### II. Choose a Location in the Source Tree for Your Starboard Port

To be perfectly compatible with the Cobalt source tree layout, any code that is
written by a party that isn't the Cobalt team should be in the
`src/third_party/` directory. The choice is up to you, but we recommend that you
follow this practice, even if, as we expect to be common, you do not plan on
sharing your Starboard implementation with anyone.

Primarily, following this convention ensures that no future changes to Cobalt or
Starboard will conflict with your source code additions. Starboard is intended
to be a junction where new Cobalt versions or Starboard implementations can be
replaced without significant (and hopefully, any) code changes.

We recommend that you place your code here in the source tree:

    src/third_party/starboard/<family-name>/

With subdirectories:

  * `shared/` - For code shared between architectures within a product family.
  * `<binary-variant>/` - For any code that is specific to a specific binary
    variant. Each one of these must at least have `configuration_public.h`,
    `atomic_public.h`, `thread_types_public.h`, `gyp_configuration.py`,
    `gyp_configuration.gypi`, and `starboard_platform.gyp` files.

In the BobCo's BobBox example, we would see something like:

  * `src/third_party/starboard/bobbox/`
      * `shared/`
      * `mipseb/`
          * `atomic_public.h`
          * `configuration_public.h`
          * `gyp_configuration.gypi`
          * `gyp_configuration.py`
          * `starboard_platform.gyp`
          * `thread_types_public.h`
      * `mipsel/`
          * `atomic_public.h`
          * `configuration_public.h`
          * `gyp_configuration.gypi`
          * `gyp_configuration.py`
          * `starboard_platform.gyp`
          * `thread_types_public.h`

And so on.


### III. Base Your Port on a Reference Port

You can start off by copying files from a reference port to your port's
location. Currently these reference ports include:

  * `src/starboard/stub`
  * `src/starboard/linux`
  * `src/starboard/raspi`

The `starboard_platform.gyp` contains absolute paths, so the paths will still be
valid if you copy it to a new directory. You can then incrementally replace
files with new implementations as necessary.

For example, if your device runs Linux, you should start from linux.

Rename the `x64x11/` directory to `<binary-variant>` (e.g. `mipseb`).

Modify the files in `<binary-variant>/` as appropriate (you will probably be
coming back to these files a lot).

Update `<binary-variant>/starboard_platform.gyp` to point at all the source
files that you want to build as your new Starboard implementation. The
`'<(DEPTH)'` expression in GYP expands to enough `../`s to take you to the
`src/` directory of your source tree. Otherwise, files are assumed to be
relative to the directory the `.gyp` or `.gypi` file is in.


### IV. Add Your Platform Configurations to cobalt_gyp

In order to use a new platform configuration in a build, you need to ensure that
you have a `gyp_configuration.py`, `gyp_configuration.gypi`, and
`starboard_platform.gypi` in their own directory for each binary variant, plus
the header files `configuration_public.h`, `atomic_public.h`, and
`thread_types_public.h`. `gyp_cobalt` will scan your directories for these
files, and then calculate a port name based on the directories between
`src/third_party/starboard` and your `gyp_configuration.*` files. (e.g. for
`src/third_party/starboard/bobbox/mipseb/gyp_configuration.py`, it would choose
the port name `bobbox_mipseb`.)

  1. Set up `gyp_configuration.py`
      1. Copy `src/starboard/linux/x64x11/gyp_configuration.py` to
         `src/third_party/starboard/<family-name>/<binary-variant>/gyp_configuration.py`.
         You may also consider copying from another reference platform, like `raspi-1`.
      1. In `gyp_configuration.py`
          1. In the `_PlatformConfig.__init__()` function, remove checks for Clang
             or GOMA.
          1. In the `CreatePlatformConfig()` function, pass your
             `<platform-configuration>` as the parameter to the _PlatformConfig
             constructor, like `return _PlatformConfig('bobbox-mipseb')`.
        1. In `GetVariables`
            1. Set `'clang': 1` if your toolchain is clang.
            1. Delete other variables in that function that are not needed for
               your platform.
            1. In `GetEnvironmentVariables`, set the dictionary values to point
               to the toolchain analogs for the toolchain for your platform.
  1. Set up `gyp_configuration.gypi`
      1. Copy `src/starboard/linux/x64x11/gyp_configuration.gypi` to
         `src/third_party/starboard/<family-name>/<binary-variant>/gyp_configuration.gypi`.
         You may also consider copying from another reference platform, like `raspi-1`.
      1. Update your platform variables.
          1. Set `'target_arch'` to your architecture: `'arm'`, `'ppc'`,
             `'x64'`, `'x86'`, `'mips'`
          1. Set `'target_os': 'linux'` if your platform is Linux-based.
          1. Set `'gl_type': 'system_gles2'` if you are using the system EGL +
             GLES2 implementation.
          1. Set `'in_app_dial'` to `1` or `0`. This enables or disables the
             DIAL server that runs inside Cobalt, only when Coblat is
             running. You do not want in-app DIAL if you already have
             system-wide DIAL support.
      1. Update your toolchain command-line flags and libraries. Make sure you
         don't assume a particular workstation layout, as it is likely to be
         different for someone else.
      1. Update the global defines in `'target_defaults'.'defines'`, if
         necessary.


You should now be able to run gyp with your new port. From your `src/` directory:

    $ cobalt/build/gyp_cobalt -C debug bobbox-mipseb
    $ ninja -C out/bobbox-mipseb_debug nplb

This will attempt to build the "No Platform Left Behind" test suite with your
new Starboard implementation, and you are ready to start porting!


## Suggested Implementation Order

When bringing up a new Starboard platform, it is suggested that you try to get
the NPLB tests passing module-by-module. Because of dependencies between
modules, you will find it easier to get some modules passing sooner than other
modules.

Here's a recommended module implementation order in which to get things going
(still significantly subject to change based on feedback):

  1. Configuration
  1. main(), Application, & Event Pump (i.e. the call into SbEventHandle)
  1. Memory
  1. Byte Swap
  1. Time
  1. String/Character/Double
  1. Log
  1. File
  1. Directory
  1. System
  1. Atomic
  1. Thread & Thread Types
  1. Mutex
  1. Condition Variable
  1. Once
  1. Socket
  1. SocketWaiter
  1. Window
  1. Input
  1. Blitter (if applicable)
  1. Audio Sink
  1. Media & Player
  1. DRM
  1. TimeZone
  1. User
  1. Storage
