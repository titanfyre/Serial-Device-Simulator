# Validation boundary

Windows x64 Release builds use Visual Studio 2022, warnings-as-errors and CFG. The protocol model is tested with known frames, framing boundaries, malformed/overflow input and 50,000 randomized input cases (50,032 total protocol assertions). AddressSanitizer instruments the model/application builds. Thirteen additional readiness checks cover first enumeration, idle readiness, later disappearance and recovery deadlines. Both the normal and current MSVC 14.44 AddressSanitizer builds pass three CTest suites. The matching private production SDK override is compiled separately and never copied into the public artifacts.

Offline GUI previews contain labelled sample data and do not create devices. A virtual COM roundtrip test exercises a different boundary from a physical serial line: it cannot establish physical baud timing, signal integrity, every client application's behavior or end-to-end game/input latency.

## Actual live result

The disposable Windows 10 VM selected the inbox `usbser.inf` package for the unique CDC identities, but device startup remained pending. The simulator did not reach readiness; no Modbus or SCPI roundtrip was verified. Normal guest shutdown subsequently stalled. Its saved state was preserved privately, then the fresh Off baseline was restored without forced power-off; all nine historical checkpoints and the disk chain were verified.

The original harness mishandled a null process exit code; its raw summary was rejected after checking the actual failed JSON results. The corrected harness requires the JSON success field and an exact native exit code. Offline assertions are not evidence of working COM transport. Release binaries remain withheld. No physical serial port was selected, and no host driver was installed or changed.

A started exact COM node is offered independently of USB idle power state, so an opening client can wake selective suspend. Device presence is revalidated every 200 ms, CDC notifications survive temporary queue backpressure, and diagnostic reports record their phase before potentially blocking SDK operations. If the kernel stalls, synchronous SDK IOCTLs and application shutdown can still block indefinitely; no bounded shutdown guarantee is made. These offline fixes have not been shown to resolve the original VM startup failure.
