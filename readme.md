PLACEHOLDER

Place the "example-v3-windows" folder provided by Sony Camera Remote Commands v2.00 into the "3rdparty/CamRmCmd_Demos" folder. 



## Sony WIA Note

For the Sony camera-control path, `SonyPTP3DevEnum::OpenByIndex()` and `WiaTransport`
currently play different roles.

- `OpenByIndex()` performs a caller-thread WIA pre-check so obvious open failures can
  be reported early.
- Long-lived command servicing is still performed by the `WiaTransport` owner STA
  thread.

This means the current implementation uses a two-stage open model: the enumerator does
an early probe, but it does not establish a persistent WIA device context for later PTP
commands. The transport layer remains the component that actually services command
traffic over time. This distinction is important for maintainers, because `OpenByIndex()`
should not be read as proof that a durable WIA device/session context has already been
created and retained.
