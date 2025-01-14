# Debugging Tips

There are many ways to debug ANGLE using generic or platform-dependent tools. Here is a list of tips
on how to use them.

## Running ANGLE under apitrace on Linux

[Apitrace](http://apitrace.github.io/) captures traces of OpenGL commands for later analysis,
allowing us to see how ANGLE translates OpenGL ES commands. In order to capture the trace, it
inserts a driver shim using `LD_PRELOAD` that records the command and then forwards it to the OpenGL
driver.

The problem with ANGLE is that it exposes the same symbols as the OpenGL driver so apitrace captures
the entry point calls intended for ANGLE and reroutes them to the OpenGL driver. In order to avoid
this problem, use the following:

1. Link your application against the static ANGLE libraries (libGLESv2_static and libEGL_static) so
   they don't get shadowed by apitrace's shim.
2. Ask apitrace to explicitly load the driver instead of using a dlsym on the current module.
   Otherwise apitrace will use ANGLE's symbols as the OpenGL driver entrypoint (causing infinite
   recursion). To do this you must point an environment variable to your GL driver.  For example:
   `export TRACE_LIBGL=/usr/lib/libGL.so.1`. You can find your libGL with
   `ldconfig -p | grep libGL`.
3. Link ANGLE against libGL instead of dlsyming the symbols at runtime; otherwise ANGLE won't use
   the replaced driver entry points. This is done with the gn arg `angle_link_glx = true`.

If you follow these steps, apitrace will work correctly aside from a few minor bugs like not being
able to figure out what the default framebuffer size is if there is no glViewport command.

For example, to trace a run of `hello_triangle`, assuming the apitrace executables are in `$PATH`:

```
gn args out/Debug # add "angle_link_glx = true"
# edit samples/BUILD.gn and append "_static" to "angle_util", "libEGL", "libGLESv2"
ninja -C out/Debug
export TRACE_LIBGL="/usr/lib/libGL.so.1" # may require a different path
apitrace trace -o mytrace ./out/Debug/hello_triangle
qapitrace mytrace
```

## Enabling General Logging

Normally, ANGLE only logs errors and warnings (e.g. to Android logcat).  General logging, or
additional levels of "trace" messages will be logged when the following GN arg is set:
```
angle_enable_trace = true
```

## Enabling Debug-Utils Markers

ANGLE can emit debug-utils markers for every GLES API command that are visible to both Android GPU
Inspector (AGI) and RenderDoc.  This support requires
[enabling general logging](#enabling-general-logging) as well as setting the following additional
GN arg:
```
angle_enable_annotator_run_time_checks = true
```
In addition, if the following GN arg is set, the API calls will output to Android's logcat:
```
angle_enable_trace_android_logcat = true
```
Once compiled, the markers need to be turned on.

### Turning on Debug Markers on Android

On Android, debug markers are turned on and off with an Android debug property that is
automatically deleted at the next reboot:

```
adb shell setprop debug.angle.markers 1
```

* 0: Turned off/disabled (default)
* 1: Turned on/enabled

### Turning on Debug Markers on Desktop

On desktop, debug markers are turned on and off with the ANGLE_ENABLE_DEBUG_MARKERS environment
variable (set in OS-specific manner):

* 0: Turned off/disabled (default)
* 1: Turned on/enabled


## Running ANGLE under GAPID on Linux

[GAPID](https://github.com/google/gapid) can be used to capture trace of Vulkan commands on Linux.
When capturing traces of gtest based tests built inside Chromium checkout, make sure to run the
tests with `--single-process-tests` argument.

## Running ANGLE under GAPID on Android

[GAPID](https://github.com/google/gapid) can be used to capture a trace of the Vulkan or OpenGL ES
command stream on Android.  For it to work, ANGLE's libraries must have different names from the
system OpenGL libraries.  This is done with the gn arg:

```
angle_libs_suffix = "_ANGLE_DEV"
```

All
[AngleNativeTest](https://chromium.googlesource.com/chromium/src/+/master/third_party/angle/src/tests/test_utils/runner/android/java/src/com/android/angle/test/AngleNativeTest.java)
based tests share the same activity name, `com.android.angle.test.AngleUnitTestActivity`.
Thus, prior to capturing your test trace, the specific test APK must be installed on the device.
When you build the test, a test launcher is generated, for example,
`./out/Release/bin/run_angle_end2end_tests`. The best way to install the APK is to run this test
launcher once.

In GAPID's "Capture Trace" dialog, "Package / Action:" should be:

```
android.intent.action.MAIN:com.android.angle.test/com.android.angle.test.AngleUnitTestActivity
```

The mandatory [extra intent
argument](https://developer.android.com/studio/command-line/adb.html#IntentSpec) for starting the
activity is `org.chromium.native_test.NativeTest.StdoutFile`. Without it the test APK crashes. Test
filters can be specified via either the `org.chromium.native_test.NativeTest.CommandLineFlags` or
the `org.chromium.native_test.NativeTest.GtestFilter` argument.  Example "Intent Arguments:" values in
GAPID's "Capture Trace" dialog:

```
-e org.chromium.native_test.NativeTest.StdoutFile /sdcard/chromium_tests_root/out.txt -e org.chromium.native_test.NativeTest.CommandLineFlags "--gtest_filter=*ES2_VULKAN"
```

or

```
-e org.chromium.native_test.NativeTest.StdoutFile /sdcard/chromium_tests_root/out.txt --e org.chromium.native_test.NativeTest.GtestFilter RendererTest.SimpleOperation/ES2_VULKAN:SimpleOperationTest.DrawWithTexture/ES2_VULKAN
```

## Running ANGLE under RenderDoc

An application running through ANGLE can confuse [RenderDoc](https://github.com/baldurk/renderdoc),
as RenderDoc [hooks to EGL](https://github.com/baldurk/renderdoc/issues/1045) and ends up tracing
the calls the application makes, instead of the calls ANGLE makes to its backend.  As ANGLE is a
special case, there's little support for it by RenderDoc, though there are workarounds.

### Windows

On Windows, RenderDoc supports setting the environment variable `RENDERDOC_HOOK_EGL` to 0 to avoid
this issue.

### Linux

On Linux, there is no supported workaround by RenderDoc.  See [this
issue](https://github.com/baldurk/renderdoc/issues/1045#issuecomment-463999869).  To capture Vulkan
traces, the workaround is to build RenderDoc without GL(ES) support.

Building RenderDoc is straightforward.  However, here are a few instructions to keep in mind.

```
# Install dependencies based on RenderDoc document.  Here are some packages that are unlikely to be already installed:
$ sudo apt install libxcb-keysyms1-dev python3-dev qt5-qmake libqt5svg5-dev libqt5x11extras5-dev

# Inside the RenderDoc directory:
$ cmake -DCMAKE_BUILD_TYPE=Release -Bbuild -H. -DENABLE_GLES=OFF -DENABLE_GL=OFF

# QT_SELECT=5 is necessary if your distribution doesn't default to Qt5
$ QT_SELECT=5 make -j -C build

# Run RenderDoc from the build directory:
$ ./build/bin/qrenderdoc
```

If your distribution does not provide a recent Vulkan SDK package, you would need to manually
install that.  This script tries to perform this installation as safely as possible.  It would
overwrite the system package's files, so follow at your own risk.  Place this script just above the
extracted SDK directory.

```
#! /bin/bash

if [ $# -lt 1 ]; then
  echo "Usage: $0 <version>"
  exit 1
fi

ver=$1

if [ ! -d "$ver" ]; then
  echo "$ver is not a directory"
fi

# Verify everything first
echo "Verifying files..."
echo "$ver"/x86_64/bin/vulkaninfo
test -f "$ver"/x86_64/bin/vulkaninfo || exit 1
echo "$ver"/x86_64/etc/explicit_layer.d/
test -d "$ver"/x86_64/etc/explicit_layer.d || exit 1
echo "$ver"/x86_64/lib/
test -d "$ver"/x86_64/lib || exit 1

echo "Verified. Performing copy..."

echo sudo cp "$ver"/x86_64/bin/vulkaninfo /usr/bin/vulkaninfo
sudo cp "$ver"/x86_64/bin/vulkaninfo /usr/bin/vulkaninfo
echo sudo cp "$ver"/x86_64/etc/explicit_layer.d/* /etc/explicit_layer.d/
sudo cp "$ver"/x86_64/etc/explicit_layer.d/* /etc/explicit_layer.d/
echo sudo rm /usr/lib/x86_64-linux-gnu/libvulkan.so*
sudo rm /usr/lib/x86_64-linux-gnu/libvulkan.so*
echo sudo cp -P "$ver"/x86_64/lib/lib* /usr/lib/x86_64-linux-gnu/
sudo cp -P "$ver"/x86_64/lib/lib* /usr/lib/x86_64-linux-gnu/

echo "Done."
```

### Android

If you are on Linux, make sure not to use the build done in the previous section.  The GL renderer
disabled in the previous section is actually needed in this section.

Define the following environment variables, for example in `.bashrc` (values are examples):

```
export JAVA_HOME=/usr/local/buildtools/java/jdk
export ANDROID_SDK=$HOME/chromium/src/third_party/android_sdk/public
export ANDROID_NDK=$HOME/chromium/src/third_party/android_ndk
export ANDROID_NDK_HOME=$HOME/chromium/src/third_party/android_ndk
```

In the renderdoc directory, create Android builds of RenderDoc:

```
mkdir build-android-arm32
cd build-android-arm32/
cmake -DBUILD_ANDROID=On -DANDROID_ABI=armeabi-v7a ..
make -j
cd ../

mkdir build-android-arm64
cd build-android-arm64/
cmake -DBUILD_ANDROID=On -DANDROID_ABI=arm64-v8a ..
make -j
cd ../
```

Note that you need both arm32 and arm64 builds even if working with an arm64 device.  See
[RenderDoc's documentation](https://github.com/baldurk/renderdoc/blob/v1.x/docs/CONTRIBUTING/Compiling.md#android)
for more information.

When you run RenderDoc, choose the "Replay Context" from the bottom-left part of the UI (defaults to
Local).  When selecting the device, you should see the RenderDoc application running.

In ANGLE itself, make sure you add a suffix for its names to be different from the system's.  Add
this to gn args:

```
angle_libs_suffix = "_ANGLE_DEV"
```

Next, you need to install an ANGLE test apk.  When you build the test, a test launcher is generated,
for example, `./out/Release/bin/run_angle_end2end_tests`. The best way to install the APK is to run
this test launcher once.

In RenderDoc, use `org.chromium.native_test` as the Executable Path, and provide the following
arguments:

```
-e org.chromium.native_test.NativeTest.StdoutFile /sdcard/chromium_tests_root/out.txt -e org.chromium.native_test.NativeTest.CommandLineFlags "--gtest_filter=*ES2_VULKAN"
```

Note that in the above, only a single command line argument is supported with RenderDoc.  If testing
dEQP on a non-default platform, the easiest way would be to modify `GetDefaultAPIName()` in
`src/tests/deqp_support/angle_deqp_gtest.cpp` (and avoid `--use-angle=X`).

## Testing with Chrome Canary

Many of ANGLE's OpenGL ES entry points are exposed in Chromium as WebGL 1.0 and WebGL 2.0 APIs that
are available via JavaScript. For testing purposes, custom ANGLE builds may be injected in Chrome
Canary.

### Setup

#### Windows

1. Download and install [Google Chrome Canary](https://www.google.com/chrome/canary/).
2. Build ANGLE x64, Release.
3. Run `python scripts\update_canary_angle.py` to replace Canary's ANGLE with your custom ANGLE
(note: Canary must be closed).

#### macOS

1. Download and install [Google Chrome Canary](https://www.google.com/chrome/canary/).
2. Clear all attributes.
   ```
   % xattr -cr /Applications/Google\ Chrome\ Canary.app
   ```
3. Build ANGLE x64 or arm64, Release.
4. Replace ANGLE libraries, adjusting paths if needed.
   ```
   % cp angle/out/Release/{libEGL.dylib,libGLESv2.dylib} /Applications/Google\ Chrome\ Canary.app/Contents/Frameworks/Google\ Chrome\ Framework.framework/Libraries
   ```
5. Re-sign the application bundle.
   ```
   % codesign --force --sign - --deep /Applications/Google\ Chrome\ Canary.app
   ```

### Usage

Run `%LOCALAPPDATA%\Google\Chrome SxS\chrome.exe` (Windows) or `./Google\ Chrome\ Canary.app/Contents/MacOS/Google\ Chrome\ Canary` (macOS) with the following command-line options:
* `--use-cmd-decoder=passthrough --use-gl=angle` and one of
  * `--use-angle=d3d9` (Direct3D 9 renderer, Windows only)
  * `--use-angle=d3d11` (Direct3D 11 renderer, Windows only)
  * `--use-angle=d3d11on12` (Direct3D 11on12 renderer, Windows only)
  * `--use-angle=gl` (OpenGL renderer)
  * `--use-angle=gles` (OpenGL ES renderer)
  * `--use-angle=vulkan` (Vulkan renderer)
  * `--use-angle=swiftshader` (SwiftShader renderer)
  * `--use-angle=metal` (Metal renderer, macOS only)
