# Harmony communications software acceptance

From the project root in PowerShell:

```powershell
./test/HarmonyCommunications/test-all.ps1
./test/HarmonyCommunications/test-all.ps1 -IncludeUsbRegression
```

The entry point runs these components sequentially and stops on the first
failed process, failed report, or missing test summary:

1. NetworkLinks: real production UDP/TCP worker loopback tests.
2. NetworkConfiguration: production UDP/TCP configuration tests.
3. HarmonyBluetoothHost: production bridge with a controlled host provider.
4. HarmonyBluetoothConfiguration: production configuration/worker integration.
5. HarmonyBluetoothSettings: the QML settings component in its host harness.
6. SPP tests and SPP SDK declaration check.
7. BLE tests and BLE SDK declaration check.
8. Optional USB regression through HarmonyUsbCdc/test-all.ps1.

All PowerShell runners execute in separate hidden child processes so their
`exit` statements preserve the aggregator. Qt builds use one build worker;
each runner selects its matching host Qt kit. The shared deveco environment
keeps user-home, temporary files, and caches within the project `tmp/` tree.
The optional USB invocation omits `-IncludeUpstream` and performs no upstream
download.

Each run creates a directory under
`tmp/verification/harmony-communications/`. Its `report.json` records component
status, exit codes, elapsed time, stdout/stderr log paths, and fresh source
report paths. Counts come from native Qt reports and actual Node summaries.
Qt initTestCase/cleanupTestCase invocations are reported separately from
functional invocations. CTest wrapper counts are not added to native counts;
unchanged reports from previous runs are excluded. SDK errors and warnings
have separate counts. Node cancelled/todo cases fail acceptance and retain
their own counts. Passed/total driver summaries are included in USB totals.

Formal runs reject QGC_TEST_FILTER, QGC_BRIDGE_SOURCE, QGC_SPP_SOURCE,
QGC_TYPESCRIPT, and QGC_ETS_SDK environment overrides. Clear these variables
before collecting the acceptance report.

After an ARM64 build, verify its contents with the software report:

```powershell
./tmp/venv/Scripts/python.exe -B ./test/HarmonyCommunications/verify-hap.py --software-report <run-directory>/report.json --version-code 5010513
```

The default selects the existing signed HAP path. Use `--build-log` and
`--build-root` for another build, and `--hap` for an explicit signed, unsigned,
or renamed HAP. The selected HAP must resolve inside the project `tmp/` tree.
For a Release delivery, for example:

```powershell
./tmp/venv/Scripts/python.exe -B ./test/HarmonyCommunications/verify-hap.py --software-report <run-directory>/report.json --version-code <code> --build-root <release-build-root> --build-log <build-log> --hap <project-tmp>/entry-default-unsigned.hap --output <project-tmp>/release-artifact.json
```

The checker reads `application-binary` from the build's generated
`QGroundControl-harmony-deployment-settings.json`. Relative paths resolve from
the build root; the resolved library must stay inside that build directory.
This supports Debug and Release without selecting a different library by
convention. It compares staged bridge sources, factory registrations,
permissions, startup icon, bytecode modules, every native library's ARM64 ELF
header, and the selected application's GNU build ID against its packaged copy.
It records source hashes, build-log and software-report hashes, the HAP hash,
and the actual library path. The default report is
`tmp/verification/qgc-communications-acceptance.json`.

Every input requires a successful build log. A HAP named `*-signed.hap` also
requires a completed or up-to-date SignHap task. `artifactVariant` records the
selected filename convention; `arktsSignedBuildPassed` describes this build-task
evidence and is false for unsigned or renamed inputs. This checker does not
perform cryptographic signature verification; `signingEvidence` states
`signatureVerification: not_performed`. A filename or successful signing task
does not establish a verified package signature.

Run the lightweight checker regression with:

```powershell
./tmp/venv/Scripts/python.exe -B -m unittest discover -s test/HarmonyCommunications -p test_verify_hap.py -v
```

Its synthetic ELF/HAP fixtures validate checker paths and rejection behavior;
they provide no evidence that a real application artifact passed. Fixtures are
created and removed under the project `tmp/` tree. A symlink escape check is
explicitly skipped if the host lacks permission to create symlinks.

This checker reports artifact checks and the supplied host report. Its action
flags apply only to that checker invocation. Independent target-runtime,
full-application, and physical reports can provide further acceptance evidence;
the checker does not evaluate those reports or issue device, Java, emulator,
installation, or build commands.
