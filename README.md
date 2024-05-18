# Memfault Zephyr Http Example

A basic HTTP-enabled Memfault example application.

Currently supporting the following boards:

- MIMXRT1024-EVK
- [WIP: no ethernet board support merged yet] FRDM-MCXN947

## Building

### Prerequisites

1. A [Zephyr build
environment](https://docs.zephyrproject.org/latest/getting_started/index.html),
used to build and flash the firmware

2. A [Memfault account](https://mflt.io/create-key/nxp-imx-rt) and a [project
   key](https://mflt.io/project-key)

### Initialize the project

```bash
# create a new zephyr workspace folder
❯ mkdir memfault-zephyr-http-example && cd memfault-zephyr-http-example
❯ git clone https://github.com/memfault/memfault-zephyr-http-example.git
❯ west init -l memfault-zephyr-http-example
❯ west update
```

### Build

Either add this line to [`prj.conf`](prj.conf):

```shell
CONFIG_MEMFAULT_PROJECT_KEY="MEMFAULT_PROJECT_KEY"
```

Or set it as a build argument:

```bash
❯ west build --pristine=always --board mimxrt1024_evk \
  memfault-zephyr-http-example \
  -- \
  -DCONFIG_MEMFAULT_PROJECT_KEY=\"MEMFAULT_PROJECT_KEY\"
```

To build for the FRDM-MCXN947 board:

```bash
❯ west build --pristine=always -b frdm_mcxn947/mcxn947/cpu0 \
  memfault-zephyr-http-example \
  -- \
  -DCONFIG_MEMFAULT_PROJECT_KEY=\"MEMFAULT_PROJECT_KEY\"
```

## Flashing

The MIIMXRT1024-EVK board has an onboard debugger that's CMSIS-DAP compatible.
You can also set a jumper to use an external debugger connecting to the SWD
header (J55).

Depending on what debugger is used, the flash command will change:

```bash
# default is J-Link
❯ west flash
# to use pyocd to attach to the CMSIS-DAP debugger
❯ west flash --runner pyocd
```
