# Matter Linux EVSE Example

An example showing the use of CHIP on the Linux. The document will describe how
to build and run CHIP Linux EVSE Example on Raspberry Pi. This doc is tested on
**Ubuntu for Raspberry Pi Server 20.04 LTS (aarch64)** and **Ubuntu for
Raspberry Pi Desktop 20.10 (aarch64)**

To cross-compile this example on x64 host and run on **NXP i.MX 8M Mini**
**EVK**, see the associated
[README document](../../../docs/platforms/nxp/nxp_imx8m_linux_examples.md) for
details.

<hr>

-   [Matter Linux EVSE Example](#matter-linux-evse-example)
    -   [Building](#building)
    -   [Commandline arguments](#commandline-arguments)
    -   [Running the Complete Example on Raspberry Pi 4](#running-the-complete-example-on-raspberry-pi-4)
    -   [Device Tracing](#device-tracing)
    -   [Python Test Cases](#python-test-cases)
        -   [Running the test cases:](#running-the-test-cases)
    -   [MATTER-REPL Interaction](#matter-repl-interaction)
        -   [Building matter-repl:](#building-matter-repl)
        -   [Activating python virtual env](#activating-python-virtual-env)
        -   [Interacting with matter-repl and the example app](#interacting-with-matter-repl-and-the-example-app)
        -   [Using matter-repl to Fake a charging session](#using-matter-repl-to-fake-a-charging-session)

<hr>

## Building

-   Install tool chain and Linux MQTT client dependency

                $ sudo apt-get install git gcc g++ python pkg-config libssl-dev libdbus-1-dev libglib2.0-dev libmosquitto-dev ninja-build python3-venv python3-dev unzip

-   Build the example application:

                $ cd ~/connectedhomeip/examples/evse-app/linux
                $ git submodule update --init
                $ source third_party/connectedhomeip/scripts/activate.sh
                $ gn gen out/debug
                $ ninja -C out/debug

    The Linux EVSE example now links against ``libmosquitto`` for the external
    MQTT bridge to EVerest, so the development package must be present on the
    build host.

-   To delete generated executable, libraries and object files use:

                $ cd ~/connectedhomeip/examples/evse-app/linux
                $ rm -rf out/

-   Build the example with pigweed RPC

                $ cd ~/connectedhomeip/examples/evse-app/linux
                $ git submodule update --init
                $ source third_party/connectedhomeip/scripts/activate.sh
                $ gn gen out/debug --args='import("//with_pw_rpc.gni")'
                $ ninja -C out/debug

## Commandline arguments

-   `--wifi`

    Enables WiFi management feature. Required for WiFi commissioning.

-   `--thread`

    Enables Thread management feature, requires ot-br-posix dbus daemon running.
    Required for Thread commissioning.

-   `--ble-controller <selector>`

    Use the specific Bluetooth controller for BLE advertisement and connections.
    For details on controller selection refer to
    [Linux BLE Settings](/platforms/linux/ble_settings.md).

-   `--featureSet <feature map for Device Energy Management e.g. 0x7a>`

    Sets the run-time FeatureMap value for the Device Energy Management cluster.
    This allows the DEM cluster to support `PFR` or `SFR` so that the full range
    of TC_DEM_2.x test cases can be exercised with this application.

    See the test-runner headers in the respective test script in
    src/python_testing/TC_DEM_2.x.py which have recommended values to use.

-   `--everest-error-history-module-id <module-id>`

    Sets the EVerest `error_history_consumer_API` module that supplies active,
    raised, and cleared errors. The default is `error_history_1`.

-   `--everest-error-origin-module-id <module-id>`

    Selects the EVSE board module whose errors drive the Matter Energy EVSE
    `FaultState`. Errors from other EVerest modules are ignored. The default is
    `bsp_1`.

-   `--everest-external-energy-limits-module-id <module-id>`

    Selects the EVerest `external_energy_limits_consumer_API` module used to
    apply Matter `MaximumChargeCurrent`. This option is unset by default;
    configure the module and provide its ID to forward the user limit.

## EVerest Fault Bridge

The bridge requests the configured Error History Consumer's active errors at
startup, then subscribes to its raised and cleared notifications. It retains
the active errors by UUID and reports the highest-priority mapped error through
the Matter Energy EVSE `FaultState` attribute and `Fault` event. Vendor warnings
do not produce a Matter fault.

The Error History Consumer and the EVSE Board Support API module must be
present in the EVerest configuration. For a development setup with the default
bridge arguments, configure module IDs `error_history_1` and `bsp_1`, and
route the connector's `bsp` connection to `bsp_1`:

```yaml
active_modules:
  error_history_1:
    module: error_history_consumer_API
    connections:
      error_history:
        - module_id: error_history
          implementation_id: error_history
  bsp_1:
    module: evse_board_support_API
    mapping:
      module:
        evse: 1
    config_module:
      cfg_communication_check_to_s: 0
  connector_1:
    connections:
      bsp:
        - module_id: bsp_1
          implementation_id: main
```

Use the command-line options above when deployment module IDs differ.

### Simulating faults

With EVerest's MQTT broker running locally, use the stable EVSE Board Support
API to raise an error:

```sh
mosquitto_pub -h 127.0.0.1 -p 1883 \
  -t everest_api/1/evse_board_support/bsp_1/m2e/raise_error \
  -m '{"type":"MREC2GroundFailure","sub_type":"matter-test","message":"ground-fault simulation"}'
```

Clear it with the same error type and subtype:

```sh
mosquitto_pub -h 127.0.0.1 -p 1883 \
  -t everest_api/1/evse_board_support/bsp_1/m2e/clear_error \
  -m '{"type":"MREC2GroundFailure","sub_type":"matter-test","message":"ground-fault simulation"}'
```

For example, `MREC2GroundFailure` maps to `GroundFault`,
`MREC4OverCurrentFailure` to `OverCurrent`, and
`MREC3HighTemperature` to `OverTemperature`. A `VendorWarning` is intentionally
ignored; unknown stopping errors map to `Other`.

## EVerest User Charge Limit Bridge

When configured with `--everest-external-energy-limits-module-id`, the bridge
forwards Matter's effective `MaximumChargeCurrent` to EVerest's stable
`external_energy_limits_consumer_API`. It publishes an import schedule with
`ac_max_current_A` in amperes.

At its first connection, the bridge first publishes an unconstrained schedule,
then records the next positive EVerest `enforced_limits` current as Matter
`CircuitCapacity`. It then begins forwarding Matter's effective limit. Later
`enforced_limits` updates are intentionally not used to change
`CircuitCapacity`, because they include the Matter external-limit contribution.
This bootstrap value represents EVerest's aggregate available capacity at that
time, including any other active EVerest constraints.

For example, when the EVSE manager exposes `evse_manager_1_api_sink` with its
`external_limits` implementation, add this module to the EVerest configuration:

```yaml
active_modules:
  matter_external_limits:
    module: external_energy_limits_consumer_API
    config_module:
      cfg_communication_check_to_s: 0
      cfg_heartbeat_interval_ms: 40000
    connections:
      energy_node:
        - module_id: evse_manager_1_api_sink
          implementation_id: external_limits
```

Then run the application with:

```sh
./chip-evse-app \
  --everest-external-energy-limits-module-id matter_external_limits
```

If a Matter user limit reduces the effective limit to `0 A`, the bridge
publishes a zero import-current limit to EVerest. The bridge republishes the
current effective limit after reconnecting to MQTT. It does not repeat baseline
capacity discovery after reconnecting.

## Running the Complete Example on Raspberry Pi 4

> If you want to test Echo protocol, please enable Echo handler
>
>     gn gen out/debug --args='chip_app_use_echo=true'
>     ninja -C out/debug

-   Prerequisites

    1. A Raspberry Pi 4 board
    2. A USB Bluetooth Dongle, Ubuntu desktop will send Bluetooth advertisement,
       which will block CHIP from connecting via BLE. On Ubuntu server, you need
       to install `pi-bluetooth` via APT.
    3. Ubuntu 20.04 or newer image for ARM64 platform.

-   Building

    Follow [Building](#building) section of this document.

-   Running

    -   [Optional] Plug USB Bluetooth dongle

        -   Plug USB Bluetooth dongle and find its bluetooth controller selector
            as described in
            [Linux BLE Settings](/platforms/linux/ble_settings.md).

    -   Run Linux EVSE Example App

                    $ cd ~/connectedhomeip/examples/evse-app/linux
                    $ sudo out/debug/chip-evse-app --ble-controller [bluetooth controller number]
                    # In this example, the device we want to use is hci1
                    $ sudo out/debug/chip-evse-app --ble-controller 1

    -   Test the device using ChipDeviceController on your laptop / workstation
        etc.

## Device Tracing

Device tracing is available to analyze the device performance. To turn on
tracing, build with RPC enabled. See [Building with RPC enabled](#building).

Obtain tracing json file.

```
    $ ./{PIGWEED_REPO}/pw_trace_tokenized/py/pw_trace_tokenized/get_trace.py -s localhost:33000 \
     -o {OUTPUT_FILE} -t {ELF_FILE} {PIGWEED_REPO}/pw_trace_tokenized/pw_trace_protos/trace_rpc.proto
```

## Python Test Cases

When you want to test this cluster you can use matter-repl or chip-tool by hand.
MATTER-REPL is slightly easier to interact with when dealing with some of the
complex structures.

There are several test scripts provided for EVSE (in
[src/python_testing](/src/python_testing)):

-   `TC_EEVSE_2_2`: This validates the primary functionality
-   `TC_EEVSE_2_3`: This validates Get/Set/Clear target commands
-   `TC_EEVSE_2_4`: This validates Faults
-   `TC_EEVSE_2_5`: This validates EVSE diagnostic command (optional)
-   `TC_EEVSE_2_6`: This validates EVSE Forecast Adjustment with State Forecast
    Reporting feature functionality
-   `TC_EEVSE_2_7`: This validates EVSE Constraints-based Adjustment with Power
    Forecast Reporting feature functionality
-   `TC_EEVSE_2_8`: This validates EVSE Constraints-based Adjustment with State
    Forecast Reporting feature functionality
-   `TC_EEVSE_2_9`: This validates EVSE Power or State Forecast Reporting
    feature functionality

These scripts require the use of Test Event Triggers via the GeneralDiagnostics
cluster on Endpoint 0. This requires an `enableKey` (16 bytes) and a set of
reserved int64_t test event trigger codes.

By default the test event support is not enabled, and when compiling the example
app you need to add `chip_enable_energy_evse_trigger=true` to the gn args.

          $ gn gen out/debug --args='chip_enable_energy_evse_trigger=true'
          $ ninja -C out/debug

Once the application is built you also need to tell it at runtime what the
chosen enable key is using the `--enable-key` command line option.

          $ ./chip-evse-app --enable-key 000102030405060708090a0b0c0d0e0f

### Running the test cases:

From the top-level of the connectedhomeip repo type:

Start the chip-evse-app:

```bash
rm -f evse.bin; out/debug/chip-evse-app --enable-key 000102030405060708090a0b0c0d0e0f --KVS evse.bin --featureSet $featureSet
```

where the \$featureSet depends on the test being run:

```
TC_DEM_2_2.py: 0x01  // PA
TC_DEM_2_3.py: 0x3b  // STA, PAU, FA, CON + (PFR | SFR)
TC_DEM_2_4.py: 0x3b  // STA, PAU, FA, CON + (PFR | SFR)
TC_DEM_2_5.py: 0x3b  // STA, PAU, FA, CON + PFR
TC_DEM_2_6.py: 0x3d  // STA, PAU, FA, CON + SFR
TC_DEM_2_7.py: 0x3b  // STA, PAU, FA, CON + PFR
TC_DEM_2_8.py: 0x3d  // STA, PAU, FA, CON + SFR
TC_DEM_2_9.py: 0x3f  // STA, PAU, FA, CON + PFR + SFR
```

where

```
PA  - DEM.S.F00(PowerAdjustment)
PFR - DEM.S.F01(PowerForecastReporting)
SFR - DEM.S.F02(StateForecastReporting)
STA - DEM.S.F03(StartTimeAdjustment)
PAU - DEM.S.F04(Pausable)
FA  - DEM.S.F05(ForecastAdjustment)
CON  -DEM.S.F06(ConstraintBasedAdjustment)
```

Then run the test:

```bash
     $ python src/python_testing/TC_EEVSE_2_2.py --endpoint 1 -m on-network -n 1234 -p 20202021 -d 3840 --hex-arg enableKey:000102030405060708090a0b0c0d0e0f
```

-   Note that the `--endpoint 1` must be used with the example, since the EVSE
    cluster is on endpoint 1. The `--hex-arg enableKey:<key>` value must match
    the `--enable-key <key>` used on chip-evse-app args.

The chip-evse-app will need to be stopped before running each test script as
each test commissions the chip-evse-app in the first step. That is also why the
evse.bin is deleted before running chip-evse-app as this is where the app stores
the matter persistent data (e.g. fabric info).

## MATTER-REPL Interaction

-   See matter-repl documentation in:
    -   [Working with Python CHIP Controller](../../../docs/development_controllers/matter-repl/python_chip_controller_building.md)
    -   [Matter_REPL_Intro](https://github.com/project-chip/connectedhomeip/blob/master/docs/development_controllers/matter-repl/Matter_REPL_Intro.ipynb)

### Building matter-repl:

```bash
    $ ./build_python.sh -i <path_to_out_folder>
```

### Activating python virtual env

-   You need to repeat this step each time you start a new shell.

```bash
    $ source <path_to_out_folder>/bin/activate
```

### Interacting with matter-repl and the example app

-   Step 1: Launch the example app

```bash
    $ ./chip-evse-app --enable-key 000102030405060708090a0b0c0d0e0f
```

-   Step 2: Launch matter-repl

```bash
    $ matter-repl
```

-   Step 3: (In matter-repl) Commissioning OnNetwork

```python
    await devCtrl.CommissionOnNetwork(1234,20202021)   # Commission with NodeID 1234
Established secure session with Device
Commissioning complete
Out[2]: <matter.native.PyChipError object at 0x7f2432b16140>
```

-   Step 4: (In matter-repl) Read EVSE attributes

```python
    # Read from NodeID 1234, Endpoint 1, all attributes on EnergyEvse cluster
    await devCtrl.ReadAttribute(1234,[(1, matter.clusters.EnergyEvse)])
```

```
{
│   1: {
│   │   <class 'matter.clusters.Objects.EnergyEvse'>: {
│   │   │   <class 'matter.clusters.Attribute.DataVersion'>: 3790455237,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.ChargingEnabledUntil'>: Null,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.FaultState'>: <FaultStateEnum.kNoError: 0>,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.NextChargeStartTime'>: Null,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.StateOfCharge'>: Null,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.MaximumChargeCurrent'>: 0,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.ApproximateEVEfficiency'>: Null,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.BatteryCapacity'>: Null,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.AcceptedCommandList'>: [
... │   │   ],
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.MinimumChargeCurrent'>: 6000,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.NextChargeTargetSoC'>: Null,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.SessionDuration'>: 758415333,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.NumberOfWeeklyTargets'>: 0,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.FeatureMap'>: 1,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.GeneratedCommandList'>: [
...
│   │   │   ],
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.State'>: <StateEnum.kNotPluggedIn: 0>,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.SessionID'>: Null,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.SessionEnergyCharged'>: Null,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.VehicleID'>: Null,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.NextChargeRequiredEnergy'>: Null,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.SessionEnergyDischarged'>: Null,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.AttributeList'>: [
... │   │   ],
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.NextChargeTargetTime'>: Null,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.CircuitCapacity'>: 0,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.DischargingEnabledUntil'>: Null,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.SupplyState'>: <SupplyStateEnum.kDisabled: 0>,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.RandomizationDelayWindow'>: 600,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.MaximumDischargeCurrent'>: 0,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.NumberOfDailyTargets'>: 1,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.UserMaximumChargeCurrent'>: 80000,
│   │   │   <class 'matter.clusters.Objects.EnergyEvse.Attributes.ClusterRevision'>: 2
│   │   }
│   }
}

```

-   Step 5: Setting up a subscription so that attributes updates are sent
    automatically

```python
   reportingTimingParams = (3, 60) # MinInterval = 3s, MaxInterval = 60s
   subscription = await devCtrl.ReadAttribute(1234,[(1, matter.clusters.EnergyEvse)], reportInterval=reportingTimingParams)
```

-   Step 6: Send an `EnableCharging` command which lasts for 60 seconds The
    `EnableCharging` takes an optional `chargingEnabledUntil` parameter which
    allows the charger to automatically disable itself at some preset time in
    the future. Note that it uses Epoch_s (which is from Jan 1 2000) which is a
    uint32_t in seconds.

```python
   from datetime import datetime, timezone, timedelta
   epoch_end = int((datetime.now(tz=timezone.utc) + timedelta(seconds=60) - datetime(2000, 1, 1, 0, 0, 0, 0, timezone.utc)).total_seconds())

   await devCtrl.SendCommand(1234, 1, matter.clusters.EnergyEvse.Commands.EnableCharging(chargingEnabledUntil=epoch_end,minimumChargeCurrent=2000,maximumChargeCurrent=25000),timedRequestTimeoutMs=3000)
```

The output should look like:

```
Attribute Changed:
{
│   'Endpoint': 1,
│   'Attribute': <class 'matter.clusters.Objects.EnergyEvse.Attributes.SupplyState'>,
│   'Value': <SupplyStateEnum.kChargingEnabled: 1>
}
Attribute Changed:
{
│   'Endpoint': 1,
│   'Attribute': <class 'matter.clusters.Objects.EnergyEvse.Attributes.MinimumChargeCurrent'>,
│   'Value': 2000
}
Attribute Changed:
{
│   'Endpoint': 1,
│   'Attribute': <class 'matter.clusters.Objects.EnergyEvse.Attributes.ChargingEnabledUntil'>,
│   'Value': 758416066
}
```

After 60 seconds the charging should automatically become disabled:

```
Attribute Changed:
{
│   'Endpoint': 1,
│   'Attribute': <class 'matter.clusters.Objects.EnergyEvse.Attributes.SupplyState'>,
│   'Value': <SupplyStateEnum.kDisabled: 0>
}
Attribute Changed:
{
│   'Endpoint': 1,
│   'Attribute': <class 'matter.clusters.Objects.EnergyEvse.Attributes.DischargingEnabledUntil'>,
│   'Value': 0
}
Attribute Changed:
{
│   'Endpoint': 1,
│   'Attribute': <class 'matter.clusters.Objects.EnergyEvse.Attributes.MinimumChargeCurrent'>,
│   'Value': 0
}
Attribute Changed:
{
│   'Endpoint': 1,
│   'Attribute': <class 'matter.clusters.Objects.EnergyEvse.Attributes.ChargingEnabledUntil'>,
│   'Value': 0
}
```

Note that you can omit the `chargingEnabledUntil` argument and it will charge
indefinitely.

### Using matter-repl to Fake a charging session

If you haven't implemented a real EVSE but want to simulate plugging in an EV
then you can use a few of the test event triggers to simulate these scenarios.

The test event triggers values can be found in:
[EnergyEvseTestEventTriggerHandler.h](../../../src/app/clusters/energy-evse-server/EnergyEvseTestEventTriggerHandler.h)

-   0x0099000000000000 - Simulates the EVSE being installed on a 32A supply
-   0x0099000000000002 - Simulates the EVSE being plugged in (this should
    generate an `EVConnected` event)
-   0x0099000000000004 - Simulates the EVSE requesting power

To send a test event trigger to the app, use the following commands (in
matter-repl):

```python
    # send 1st event trigger to 'install' the EVSE on a 32A supply
    await devCtrl.SendCommand(1234, 0, matter.clusters.GeneralDiagnostics.Commands.TestEventTrigger(enableKey=bytes([b for b in range(16)]), eventTrigger=0x0099000000000000))

    # send 2nd event trigger to plug the EV in
    await devCtrl.SendCommand(1234, 0, matter.clusters.GeneralDiagnostics.Commands.TestEventTrigger(enableKey=bytes([b for b in range(16)]), eventTrigger=0x0099000000000002))

```

Now send the enable charging command (omit the `chargingEnabledUntil` arg this
time):

```python
    await devCtrl.SendCommand(1234, 1, matter.clusters.EnergyEvse.Commands.EnableCharging(minimumChargeCurrent=2000,maximumChargeCurrent=25000),timedRequestTimeoutMs=3000)
```

Now send the test event trigger to simulate the EV asking for demand:

```python
    # send 2nd event trigger to plug the EV in
    await devCtrl.SendCommand(1234, 0, matter.clusters.GeneralDiagnostics.Commands.TestEventTrigger(enableKey=bytes([b for b in range(16)]), eventTrigger=0x0099000000000004))

    # Read the events
    await devCtrl.ReadEvent(1234,[(1, matter.clusters.EnergyEvse,1)])
```

```
[
│   EventReadResult(
│   │   Header=EventHeader(
│   │   │   EndpointId=1,
│   │   │   ClusterId=153,
│   │   │   EventId=0,
│   │   │   EventNumber=65538,
│   │   │   Priority=<EventPriority.INFO: 1>,
│   │   │   Timestamp=1705102500069,
│   │   │   TimestampType=<EventTimestampType.EPOCH: 1>
│   │   ),
│   │   Status=<Status.Success: 0>,
│   │   Data=EVConnected(
│   │   │   sessionID=0
│   │   )
│   ),
│   EventReadResult(
│   │   Header=EventHeader(
│   │   │   EndpointId=1,
│   │   │   ClusterId=153,
│   │   │   EventId=2,
│   │   │   EventNumber=65539,
│   │   │   Priority=<EventPriority.INFO: 1>,
│   │   │   Timestamp=1705102801764,
│   │   │   TimestampType=<EventTimestampType.EPOCH: 1>
│   │   ),
│   │   Status=<Status.Success: 0>,
│   │   Data=EnergyTransferStarted(
│   │   │   sessionID=0,
│   │   │   state=<StateEnum.kPluggedInCharging: 3>,
│   │   │   maximumCurrent=25000
│   │   )
│   )
]
```

-   We can see that the `EventNumber 65538` was sent when the vehicle was
    plugged in, and a new `sessionID=0` was created.
-   We can also see that the `EnergyTransferStarted` was sent in
    `EventNumber 65539`

What happens when we unplug the vehicle?

```python
    await devCtrl.SendCommand(1234, 0, matter.clusters.GeneralDiagnostics.Commands.TestEventTrigger(enableKey=bytes([b for b in range(16)]), eventTrigger=0x0099000000000001))
```

When we re-read the events:

```
[
│   EventReadResult(
│   │   Header=EventHeader(
│   │   │   EndpointId=1,
│   │   │   ClusterId=153,
│   │   │   EventId=3,
│   │   │   EventNumber=65540,
│   │   │   Priority=<EventPriority.INFO: 1>,
│   │   │   Timestamp=1705102996749,
│   │   │   TimestampType=<EventTimestampType.EPOCH: 1>
│   │   ),
│   │   Status=<Status.Success: 0>,
│   │   Data=EnergyTransferStopped(
│   │   │   sessionID=0,
│   │   │   state=<StateEnum.kPluggedInCharging: 3>,
│   │   │   reason=<EnergyTransferStoppedReasonEnum.kOther: 2>,
│   │   │   energyTransferred=0
│   │   )
│   ),
│   EventReadResult(
│   │   Header=EventHeader(
│   │   │   EndpointId=1,
│   │   │   ClusterId=153,
│   │   │   EventId=1,
│   │   │   EventNumber=65541,
│   │   │   Priority=<EventPriority.INFO: 1>,
│   │   │   Timestamp=1705102996749,
│   │   │   TimestampType=<EventTimestampType.EPOCH: 1>
│   │   ),
│   │   Status=<Status.Success: 0>,
│   │   Data=EVNotDetected(
│   │   │   sessionID=0,
│   │   │   state=<StateEnum.kPluggedInCharging: 3>,
│   │   │   sessionDuration=0,
│   │   │   sessionEnergyCharged=0,
│   │   │   sessionEnergyDischarged=0
│   │   )
│   )
]

```

-   In `EventNumber 65540` we had an `EnergyTransferStopped` event with reason
    `kOther`.

    This was a rather abrupt end to a charging session (normally we would see
    the EVSE or EV decide to stop charging), but this demonstrates the cable
    being pulled out without a graceful charging shutdown.

-   In `EventNumber 65541` we had an `EvNotDetected` event showing that the
    state was `kPluggedInCharging` prior to the EV being not detected (normally
    in a graceful shutdown this would be `kPluggedInNoDemand` or
    `kPluggedInDemand`).
