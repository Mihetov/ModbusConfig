# ModbusConfig

## Startup parameters

The executable supports startup configuration through CLI arguments:

- `--mode <api|headless>`: run HTTP API mode or core-only headless mode.
- `--bind <ip>`: API bind address (default `0.0.0.0`).
- `--api-port <port>`: API listen port (default `8080`).
- `--transport <none|tcp|rtu>`: open transport at startup.

TCP startup:
- `--tcp-host <ip>`
- `--tcp-port <port>`

RTU startup:
- `--rtu-port <serial_port>`
- `--rtu-baud <baudrate>`
- `--rtu-stop-bits <1|2>`

Other:
- `--verbose-modbus`: print incoming Modbus JSON responses.
- `--help`: print usage.

### Example

```bash
./ModbusConfig --mode api --bind 0.0.0.0 --api-port 8080 --transport tcp --tcp-host 192.168.0.10 --tcp-port 502
```
