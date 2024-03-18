# Memfault Zephyr Http Example

A basic HTTP-enabled Memfault example application.

Currently supporting the following boards:

- MIMXRT1024-EVK
- [WIP: no ethernet board support merged yet] FRDM-MCXN947

## Building

After setting up a [Zephyr build environment](https://docs.zephyrproject.org/latest/getting_started/index.html), you can build the example application with the following commands:

```bash
# create a new zephyr workspace folder
❯ mkdir memfault-zephyr-http-example && cd memfault-zephyr-http-example
❯ git clone https://github.com/memfault/memfault-zephyr-http-example.git
❯ west init -l memfault-zephyr-http-example
❯ west update
❯ west build --pristine=always --board mimxrt1024_evk \
  memfault-zephyr-http-example \
  -- \
  -DEXTRA_CONF_FILE=overlay-http.conf \
  -DCONFIG_MEMFAULT_PROJECT_KEY=\"MEMFAULT_PROJECT_KEY\"
```

To build for the FRDM-MCXN947 board:

```bash
❯ west build --pristine=always -b frdm_mcxn947/mcxn947/cpu0 \
  memfault-zephyr-http-example \
  -- \
  -DCONFIG_MEMFAULT_PROJECT_KEY=\"$(<~/.memfault-noah-test-project-key)\"
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
