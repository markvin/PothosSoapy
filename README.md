# Pothos SDR blocks

SDR source and sink blocks for interfacing SDR hardware with Pothos.
The blocks use SoapySDR (https://github.com/pothosware/SoapySDR) to
control the hardware.

This branch (`pothos2`) builds against the modernized core
(https://github.com/markvin/PothosCore, branch `pothos2`). The modules use
only the public API of the core.

## Dependencies

- an installed Pothos core (`find_package(Pothos)`);
- SoapySDR 0.8 or higher;
- nlohmann_json 3.11 or higher (headers only);
- CMake 3.25 or higher.

CMake finds the dependencies with `find_package`. SoapySDR comes from the
system packages or from a build from source (give its prefix in
`CMAKE_PREFIX_PATH`). nlohmann_json comes from the system packages or from
Conan 2 (`conanfile.txt`). A missing dependency stops the configure step.

SoapySDR 0.8 includes `<ciso646>`, and GCC 15 or higher gives a warning for
it. With a SoapySDR older than 0.9, the modules ignore this `#warning`.
SoapySDR 0.9 does not have this problem.

## Build

With system packages (for example Arch Linux: `soapysdr nlohmann-json`):

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=<core prefix>
cmake --build build
ctest --test-dir build --output-on-failure
```

With SoapySDR from source and json from Conan 2:

```sh
conan install . --output-folder=build/conan -s build_type=Release -s compiler.cppstd=23 --build=missing
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=build/conan/conan_toolchain.cmake \
    -DCMAKE_PREFIX_PATH="<core prefix>;<SoapySDR prefix>"
cmake --build build
ctest --test-dir build --output-on-failure
```

`<core prefix>` is the installation prefix of the Pothos core. Do not set
it when the core is installed in a default location of the system. When
SoapySDR is not in a system directory, its library directory goes into the
install RPATH of the modules (Linux and macOS). On Windows, put the `bin`
directory of SoapySDR on `PATH`.

Install the modules:

```sh
cmake --install build
```

The core loads modules only from its own prefix (and from
`POTHOS_PLUGIN_PATH`). Thus the default install prefix of this project is
the prefix of the core. When you give `CMAKE_INSTALL_PREFIX`, set
`POTHOS_PLUGIN_PATH` to `<prefix>/lib/Pothos/modules<ABI version>`.

## Tests

The self-tests need no SDR hardware. A test driver of SoapySDR
(`pothos_soapy_test`) is a part of the `SoapySupport` module. A test
registers the driver only while it runs, so the driver is never in the
device list of the user. The data of `/soapy/sink` goes through the driver
to `/soapy/source`.

The self-tests also use blocks of PothosBlocks (for example
`/blocks/feeder_source` and `/blocks/collector_sink`). Before you run the
tests, install PothosBlocks (https://github.com/markvin/PothosBlocks,
branch `pothos2`) into the prefix of the core. The build of soapy does not
need PothosBlocks.

Each module lists its self-tests (`TESTS` of `soapy_module()`). The test
`soapy.stage` installs the modules into `build/stage`. Then
`soapy.selftests` runs each self-test with the PothosUtil of the core and
`POTHOS_PLUGIN_PATH` set to the staged modules.

`soapy.selftests` stops with an error in these conditions:

- PothosBlocks is not installed in the prefix of the core;
- the soapy modules are also installed in the prefix of the core (the
  core would load the two copies). Remove the installed copy, or test
  against another installation of the core.

### Test with a device

To do a check with real hardware, give the device args of SoapySDR. For
example, an RTL-SDR receiver with the SoapyRTLSDR module:

```sh
cmake -S . -B build -DSOAPY_TEST_DEVICE=driver=rtlsdr
cmake --build build
ctest --test-dir build -R soapy.device -V
```

The test `soapy.device` receives from `/soapy/source` for approximately
one second. It prints the number of samples and the rate. It fails when no
samples come. Without `SOAPY_TEST_DEVICE`, the CTest test `soapy.device`
does not exist. Its self-test `/soapy/device_tests/test_source` is always
in the module: without the variable `SOAPY_TEST_DEVICE`, it prints a
message and passes. Thus `PothosUtil --self-tests` needs no device.

## Documentation

* https://github.com/pothosware/PothosSoapy/wiki

## Licensing information

Use, modification and distribution is subject to the Boost Software
License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
http://www.boost.org/LICENSE_1_0.txt)
