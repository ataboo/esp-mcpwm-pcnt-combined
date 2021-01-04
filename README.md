# ESP MCPWM PCNT Combined Test

A simple example of using espressif's MCPWM and PCNT peripherals using the same pins for ESP-32.  Follows [clarkster's solution to using the same pin for outputting PWM while counting at the same time](https://www.esp32.com/viewtopic.php?t=4953).  In this case MCPWM is used instead of RMT.

In my setup, I am driving a stepper driver so `TICK_PIN` is providing the pulse and `DIR_PIN` is selecting the direction.  `USE_LOOPBACK` is an alternate configuration
where the `TICK_PIN` is shorted back to `GPIO_5`.

## Install/Run

Built using esp-idf `v4.2 release`.  It looks like RMT's event methods will change soon so that may need to be updated.

### Dumping Register Values

The register values printed between setup steps will need to be changed to match the pins configured at the top.
