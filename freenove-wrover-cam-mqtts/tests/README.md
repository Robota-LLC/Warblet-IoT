# Host checks

From the demo directory, with GCC available:

```sh
gcc -std=c11 -Wall -Wextra -Werror -Imain tests/camera_health_test.c main/camera_health.c -o build/camera_health_test
build/camera_health_test
gcc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable -Itests/stubs -Imain tests/main_report_test.c main/camera_health.c -o build/main_report_test
build/main_report_test
```

The second harness compiles the application's actual startup and report code. Fake queues and a clock exercise returned failures, recovery, retry limits, hung operations, late completions, allocation and publish failures, periodic fault health, explicit failed Snap, and image-last ordering. It verifies networking starts without calling the camera driver. It does not simulate interrupts or establish hardware safety.

From a Warblet core checkout (to use its pinned Starlark dependency):

```sh
go run <path-to-this-demo>/tests/decoder_check.go <path-to-this-demo>/decoder.star
```

The decoder check supplies the core's real metadata keys, which do not include a message tag. Its adapter accepts bytes for `json.decode`, as the core does. It validates the decoder and scalar output shape, not the core's process/resource limits. Run the core's decoder integration checks when changing the installed spec or catalogue.

`idf.py build` checks the ESP32 firmware against the pinned camera component. Physical camera failure, continued accepted health and settings preservation require an identified board with a verified recovery backup; host checks alone do not establish those outcomes.
