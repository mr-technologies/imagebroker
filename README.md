# `imagebroker`

`imagebroker` application demonstrates how to export images to the user code from C API of [MRTech IFF SDK](https://mr-te.ch/iff-sdk).
It is located in `samples/02_export` directory of IFF SDK package.
Application comes with example configuration file (`imagebroker.json`) providing the following functionality:

* acquisition from XIMEA camera
* color pre-processing on GPU:
  * black level subtraction
  * histogram calculation
  * white balance
  * demosaicing
  * color correction
  * gamma
  * image format conversion
* automatic control of exposure time and white balance
* image export to the user code

Additionally example code renders images on the screen using [OpenCV](https://opencv.org/) library, which should be installed in the system (minimal required version is 4.5.2).
