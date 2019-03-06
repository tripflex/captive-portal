# Mongoose OS Captive Portal

[![Gitter](https://badges.gitter.im/cesanta/mongoose-os.svg)](https://gitter.im/cesanta/mongoose-os?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge)

- [Mongoose OS Captive Portal](#mongoose-os-captive-portal)
  - [Captive Portal Stack](#captive-portal-stack)
  - [Author](#author)
  - [Why?](#why)
  - [Features](#features)
  - [Settings](#settings)
  - [Installation/Usage](#installationusage)
  - [Required Libraries](#required-libraries)
  - [How it works](#how-it-works)
      - [Known Endpoints](#known-endpoints)
      - [Samsung Device Caveots](#samsung-device-caveots)
      - [Android `/generate_204` Handling](#android-generate_204-handling)
  - [Available Functions/Methods](#available-functionsmethods)
    - [C Functions](#c-functions)
  - [Use specific branch of library](#use-specific-branch-of-library)
  - [Changelog](#changelog)
  - [License](#license)

This is a captive portal library for Mongoose OS, it responds to all DNS queries returning the device's IP address, as well as adding known Captive Portal endpoints for mobile/desktop devices, to allow you to serve a custom HTML file from the device, as your own custom captive portal.

## Captive Portal Stack

This is the captive portal library from the [Captive Portal WiFi Full Stack](https://github.com/tripflex/captive-portal-wifi-stack), a full stack (frontend web ui & backend handling) library for implementing a full Captive Portal WiFi with Mongoose OS

## Author
Myles McNamara ( https://smyl.es )

## Why?
You may be wondering why did I create my own, when Mongoose OS has a Captive Portal example application.  Well ... if you have used that example app you would know that it is VERY basic, and is basically just a catch-all DNS responder. It also does not have GZIP handling, support for known mobile endpoints, and many of the features included in this library.  That's why :)

## Features
- Mobile and desktop devices prompt the "Login to network" window/notification
- Support for GZIP files
- Support for Samsung Android devices that do not follow Captive Portal 302 redirect standards

## Settings
Check the `mos.yml` file for latest settings, all settings listed below are defaults

```yaml
  - [ "cportal", "o", {title: "Captive Portal configurations"}]
  - [ "cportal.enable", "b", true, {title: "Enable WiFi captive portal on device boot"}]
  - [ "cportal.hostname", "s", "setup.device.local", {title: "Hostname to use for captive portal redirect"}]
  - [ "cportal.index", "s", "index.html", {title: "Filename of HTML file to use when serving the captive portal index file"}]
```

## Installation/Usage
Add this lib your `mos.yml` file under `libs:`

```yaml
  - origin: https://github.com/tripflex/captive-portal
```

## Required Libraries
*These libraries are already defined as dependencies of this library, and is just here for reference (you're probably already using these anyways)*
- [boards](https://github.com/mongoose-os-libs/boards)
- [ca-bundle](https://github.com/mongoose-os-libs/ca-bundle)
- [http-server](https://github.com/mongoose-os-libs/http-server)

## How it works
When device boots up, if `cportal.enable` is set to `true` (default is `false`) captive portal is initialized. If `cportal.enable` is not set to `true` you must call `mgos_captive_portal_start` in C (mJS to be added later)

#### Known Endpoints
Initialization enables a DNS responder for any `A` DNS record, that responds with the device's IP address.  Captive Portal also adds numerous HTTP endpoints for known Captive Portal device endpoints:
- `/mobile/status.php` Android 8.0 (Samsung s9+)
- `/generate_204` Android
- `/gen_204` Android
- `/ncsi.txt` Windows
- `/hotspot-detect.html` iOS
- `/hotspotdetect.html` iOS
- `/library/test/success.html` iOS
- `/success.txt` OSX
- `/kindle-wifi/wifistub.html` Kindle

A root endpoint is also added, `/` to detect `CaptiveNetworkSupport` in the User-Agent of device, to redirect to captive portal.

When one of these endpoints is detected from a device (mobile/desktop), it will automatically redirect (with a `302` redirect), to the config value from `cportal.hostname` (default is `setup.device.local`).

If on a mobile device, the user should be prompted to "Login to Wifi Network", or on desktop with captive portal support, it should open a window.

The root endpoint is also used, to detect the value in the `Host` header, and if it matches the `cportal.hostname` value, we assume the access is meant for the captive portal.  This allows you to serve HTML files via your device, without captive portal taking over the `index.html` file (but you must specify the custom file to use in `cportal.index`)

#### Samsung Device Caveots
Samsung devices seem to be one of the only ones with captive portal issues, which I determined was due to those devices not following the standard of when a 302 redirect is returned, that is means there's a captive portal.

See [the issue from my original Wifi Captive Portal library](https://github.com/tripflex/wifi-captive-portal/issues/7) for more details

The workaround for this is described below in how this library handles the `generate_204` endpoint for Android devices

#### Android `/generate_204` Handling
As mentioned above -- Samsung devices suck.  The workaround for this was instead of returning a `302` redirect, to return a `200` response, with content in the response.  Most threads or questions you find online regarding this say to just return the captive portal splash page ... while this would work, this was not the approach I wanted to take.

The reason I did not want to use the above approach was because this would result in the captive portal login window that is shown on the device, showing `connectivitycheck.google.com` or something similar for the URL.  I want this to match the hostname set in configuration, and as such, instead of returning the splash page, I return a dynamically generated HTML page that includes an HTML `meta` refresh tag, that immediately refreshes the page, to the captive portal URL.  This "refresh" will be completely transparent to the end user, and will result in the device showing the correct captive portal URL.

## Available Functions/Methods

### C Functions
```C
bool mgos_captive_portal_start(void)
```

## Use specific branch of library
To use a specific branch of this library (as example, `dev`), you need to specify the `version` below the library

```yaml
  - origin: https://github.com/tripflex/captive-portal
   version: dev
```

## Changelog

**1.0.0** TBD - Initial release

## License
Apache 2.0
