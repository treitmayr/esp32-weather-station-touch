# Weather Station for Color Kit Grande and ESP32 TouchDown

Weather station application for the [ThingPulse Color Kit Grande](https://thingpulse.com/product/esp32-wifi-color-display-kit-grande/) and the [ESP32 TouchDown](https://github.com/DustinWatts/esp32-touchdown).

## Differences to Upstream Version

This fork provides the following added features compared to the upstream version:

* **Landscape Mode:** This is particularly useful when the device will actually be touched occasionally (e.g. beside the bed) and therefore needing a more stable case.
* **Automatic Screen Off:** The screen can be configured to turn off after a specific time, and turn back on as soon as the display is touched. Transitions between on and off are implemented in a smooth way.
* **Today's Temperature Trend:** In landscape mode, the wind rose is replaced by a tempature trend for the next 15 hours.
* **ESP32 TouchDown:** This target hardware is now directly supported via a separate build environment for PlatformIO.
* **SNTP Server via DHCP:** The SNTP server (responsible for delivering the current date and time) will be obtained via DHCP, if supported.

Fixes to the upstream version:

* **Stable Time Display**: The upstream version tended to skip a second now and then due to a display loop period of slightly longer then 1 second. This is fixed now.


## Exemplary Variations

### ESP32 TouchDown (landscape mode)

[![ESP32 TouchDown with weather station application](assets/ESP32-TouchDown.jpg)](https://github.com/DustinWatts/esp32-touchdown)

### Color Kit Grande (portrait mode)

[![Color Kit Grande with sample application: weather station](https://thingpulse.com/wp-content/uploads/2022/10/ThingPulse-Color-Kit-Grand-with-sample-application.jpg)](https://thingpulse.com/product/esp32-wifi-color-display-kit-grande/)

## How to use it

See the documentation at https://docs.thingpulse.com/guides/esp32-color-kit-grande/.

## Service level promise

<table><tr><td><img src="https://thingpulse.com/assets/ThingPulse-open-source-prime.png" width="150">
</td><td>This is a ThingPulse <em>prime</em> project. See our <a href="https://thingpulse.com/about/open-source-commitment/">open-source commitment declaration</a> for what this means.</td></tr></table>
