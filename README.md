# jesse-cube
Throwaway repo for testing OpenGL->Vulkan interop and HDR-10 on Linux and Windows-10.

# Linux:

## Requirements:

For use on Linux you need a modern AMD graphics card with HDR support. It has been tested with AMD Radeon Pro 560 (Polaris),
Raven Ridge (Vega 11 + DCN-1) in a PC with Ryzen-5 2400G integrated processor graphics.

You also need a 64-Bit distribution with at least X-Server 1.20 and sufficiently modern Mesa,
tested with Ubuntu 19.10 (Mesa 19.2) and Ubuntu 20.04-LTS (Mesa 20.0). Ubuntu 18.04.4-LTS with
latest HWE stack installed may work, but is untested. Ubuntu 20.04-LTS is recommended.

For HDR-10 testing you will need the AMD open-source Vulkan driver amdvlk:

https://github.com/GPUOpen-Drivers/AMDVLK/releases

As of 26th June 2020 you need amdvlk v-2020.Q2.5, which has been successfully tested to work correctly. The following link provides a debian package which should work on Ubuntu 18.04 LTS and later, tested on Ubuntu 19.10 and 20.04 LTS:

https://github.com/GPUOpen-Drivers/AMDVLK/releases/download/v-2020.Q2.5/amdvlk_2020.Q2.5_amd64.deb

For basic non-HDR testing, the distribution provided RADV AMD Vulkan driver is sufficient.

## Use:

With RADV for basic testing on the first display:

``./cube-display --gpu 0``

With RADV for basic testing on a specified display output (``xrandr`` lists available outputs), e.g., DisplayPort-0

``./cube-display --gpu 0 --output DisplayPort-0``

With amdvlk for HDR testing on the first display:

``./cube-display --gpu 1``

With amdvlk for basic testing on a specified display output (``xrandr`` lists available outputs), e.g., DisplayPort-0

``./cube-display --gpu 1 --output DisplayPort-0``

The default picture is that of a rotating cute cat. Other less cute, but more meaningful test pictures can be
selected via the optional ``--testpattern x`` switch, e.g., ``--testpattern 1``

Quit the demo by pressing the ``q`` key.

# Windows-10:

## Requirements:

For use on Windows-10 you need a modern AMD or NVidia graphics card with HDR support. It has been tested with
AMD Raven Ridge (Vega 11 + DCN-1) in a PC with Ryzen-5 2400G integrated processor graphics and with a NVidia
GeForce GTX-1650 discrete gpu.

## Use:

In a command shell window, go to the folder and type:

``hdrglinterop.exe`` or Double-click on the hdrglinterop.exe icon as usual.

On a multi-display setup, the monitor on which the mouse pointer resides at launch time is the one
that will be used.

The default picture is that of a rotating cute cat. Press the ``q`` key to quit the display, press any
key to close the debug window with diagnostic text output.

Other less cute, but more meaningful test pictures can be selected when launching from the
command shell window via the optional ``--testpattern x`` switch, e.g., ``--testpattern 1``

# Further use on all operating systems (command line switches):

``--help`` provides command line help.

``--testpattern x`` for ``x`` equals:
- 0 = Jesse the cat rotating.
- 1 = Draw a white rectangular patch, covering the central 10% of the display.
- 2 = Like 1, but flash the center patch on and off every couple of seconds.
- 3 = Like 1, but move the patch around on the display.
- 4 = Draw the complete display in one uniform color.
- 5 = Like 4, but alternate between color and a black display every couple of seconds.

For testpattern 1 and 2, the option ``--translate x y`` allows to shift the patch by a certain
fraction of the display width and height, e.g., ``--translate 0.25 0.5`` to move it 0.25 display
widths to the right and 0.5 display height down.

The option ``--rgb r g b``` allows to define the color and brightness of the test stimuli
in nits, e.g., ``--rgb 500 300 100`` would request a color with a red intensity of 500 nits,
green intensity of 300 nits and blue intensity of 100 nits.

By default, a 10 bit per color channel output precision is requested if supported by the
gpu and display, with a fallback to 8 bit if 10 bit is unsupported. You can request a
different precision via ``--format x`` for values of x of:

- 0 = 8 bpc
- 1 = 10 bpc with fallback to 8 bpc.
- 2 = 16 bit floating point with fallback to 10 bpc or 8 bpc.

The demo will render its test images for HDR-10, BT-2020 color space, with the
ST-2084 PQ "Perceptual Quantizer" OETF for decoding and display by a suitable
HDR display.

# More commandline options:

``--localdimming`` Request that the HDR monitor use local backlight dimming. Needs a AMD gpu on Windows-10
and a FreeSync2 HDR capable HDR monitor.

``--timestamp`` Proof of concept of basic timestamping of presentation.

``--ifi x`` Wait x milliseconds between presents, for some control of framerate.

