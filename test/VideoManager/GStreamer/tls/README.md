# RTSPS test certificates

These public fixtures identify `localhost` and `127.0.0.1` and are valid from 2020-01-01 through 2120-01-01.

`ca-certificate.data` signs `server-certificate.data`. `self-signed-certificate.data` uses the same server key and has no trusted issuer in `ca-certificate.data`.

The public RSA test key comes from [GLib Networking 2.80.1](https://github.com/GNOME/glib-networking/blob/2.80.1/tls/tests/files/server-key-pkcs8.pem), distributed under LGPL-2.1-or-later. During HarmonyOS test configuration, CMake downloads this fixture to the build directory and verifies SHA256 `e0df67867769430ae32b6088db6158378de3d87f78e97cd3688444844318ea35`. A matching local `server-public-test-key.data` can be supplied for offline builds. Its resource path remains `/GStreamerTests/tls/server-public-test-key.data`.

These fixture keys contain no deployment credentials. The `.data` certificate files contain PEM text.
