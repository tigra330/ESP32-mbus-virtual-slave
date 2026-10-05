import serial
import time

PORT = "/dev/ttyUSB0"

START_ADDRESS = 1
END_ADDRESS = 10

TIMEOUT_PER_METER = 0.8  # Sekunden


def checksum(data):
    return sum(data) & 0xFF


def build_req_ud2(address):
    control = 0x5B
    cs = (control + address) & 0xFF

    return bytes([
        0x10,
        control,
        address,
        cs,
        0x16
    ])


def manufacturer_decode(value):
    c1 = ((value >> 10) & 0x1F) + 64
    c2 = ((value >> 5) & 0x1F) + 64
    c3 = (value & 0x1F) + 64

    return chr(c1) + chr(c2) + chr(c3)


def bcd_id(data):
    result = ""

    for b in reversed(data):
        result += f"{(b >> 4) & 0x0F}{b & 0x0F}"

    return result.lstrip("0") or "0"


def read_long_frame(ser, timeout):
    start_time = time.monotonic()

    # Auf erstes 0x68 warten
    while time.monotonic() - start_time < timeout:
        b = ser.read(1)

        if not b:
            continue

        if b[0] == 0x68:
            break
    else:
        return None

    remaining_timeout = timeout - (time.monotonic() - start_time)

    if remaining_timeout <= 0:
        return None

    ser.timeout = remaining_timeout

    header = ser.read(3)

    if len(header) != 3:
        return None

    length1 = header[0]
    length2 = header[1]
    start2 = header[2]

    if length1 != length2:
        raise ValueError("Ungültiger M-Bus Frame: Längenbytes unterschiedlich")

    if start2 != 0x68:
        raise ValueError("Ungültiger M-Bus Frame: zweites 0x68 fehlt")

    remaining_timeout = timeout - (time.monotonic() - start_time)

    if remaining_timeout <= 0:
        return None

    ser.timeout = remaining_timeout

    rest = ser.read(length1 + 2)

    if len(rest) != length1 + 2:
        return None

    return bytes([0x68]) + header + rest


def parse_first_record(data):
    if len(data) < 2:
        return None

    pos = 0

    dif = data[pos]
    pos += 1

    if dif in (0x0F, 0x2F):
        return None

    vif = data[pos]
    pos += 1

    data_type = dif & 0x0F

    lengths = {
        0x00: 0,
        0x01: 1,
        0x02: 2,
        0x03: 3,
        0x04: 4,
        0x05: 4,
        0x06: 6,
        0x07: 8,
    }

    if data_type not in lengths:
        return None

    value_len = lengths[data_type]

    if pos + value_len > len(data):
        return None

    raw = data[pos:pos + value_len]

    if value_len == 0:
        return None

    value = int.from_bytes(
        raw,
        byteorder="little",
        signed=False
    )

    unit = ""
    scaled_value = value

    # Volumen in m³
    if 0x10 <= vif <= 0x17:
        exponent = (vif & 0x07) - 6
        scaled_value = value * (10 ** exponent)
        unit = "m³"

    # Energie Wh
    elif 0x00 <= vif <= 0x07:
        exponent = (vif & 0x07) - 3
        scaled_value = value * (10 ** exponent)
        unit = "Wh"

    return {
        "dif": dif,
        "vif": vif,
        "raw": value,
        "value": scaled_value,
        "unit": unit
    }


def parse_frame(frame):
    length = frame[1]

    body = frame[4:4 + length]

    received_cs = frame[4 + length]
    calculated_cs = checksum(body)

    if received_cs != calculated_cs:
        raise ValueError(
            f"Checksumme falsch: RX=0x{received_cs:02X}, "
            f"berechnet=0x{calculated_cs:02X}"
        )

    if frame[5 + length] != 0x16:
        raise ValueError("Stopbyte 0x16 fehlt")

    control = body[0]
    address = body[1]
    ci = body[2]

    result = {
        "address": address,
        "control": control,
        "ci": ci,
        "raw_frame": frame.hex(" ").upper()
    }

    if ci != 0x72:
        return result

    data = body[3:]

    if len(data) < 12:
        return result

    result["ident"] = bcd_id(data[0:4])

    manufacturer_raw = data[4] | (data[5] << 8)

    result["manufacturer"] = manufacturer_decode(
        manufacturer_raw
    )

    result["version"] = data[6]
    result["medium"] = data[7]
    result["access_number"] = data[8]
    result["status"] = data[9]

    records = data[12:]

    result["record"] = parse_first_record(records)

    return result


def scan_meter(ser, address):
    frame = build_req_ud2(address)

    ser.reset_input_buffer()

    print(
        f"Adresse {address:3d}: sende "
        f"{frame.hex(' ').upper()}",
        end=""
    )

    ser.write(frame)
    ser.flush()

    response = read_long_frame(
        ser,
        TIMEOUT_PER_METER
    )

    if response is None:
        print("  -> Timeout / nicht vorhanden")
        return

    try:
        meter = parse_frame(response)

    except Exception as e:
        print(f"  -> Fehler beim Parsen: {e}")
        print("     RAW:", response.hex(" ").upper())
        return

    print("  -> gefunden")

    print(
        f"     Primäradresse:  "
        f"{meter.get('address')}"
    )

    if "ident" in meter:
        print(
            f"     Sekundär-ID:     "
            f"{meter['ident']}"
        )

        print(
            f"     Hersteller:      "
            f"{meter['manufacturer']}"
        )

        print(
            f"     Medium:          "
            f"0x{meter['medium']:02X}"
        )

        print(
            f"     Access Number:   "
            f"{meter['access_number']}"
        )

    record = meter.get("record")

    if record:
        if record["unit"]:
            print(
                f"     Zählerwert:      "
                f"{record['value']} "
                f"{record['unit']}"
            )
        else:
            print(
                f"     Zählerwert RAW:  "
                f"{record['raw']}"
            )

        print(
            f"     DIF/VIF:         "
            f"0x{record['dif']:02X} / "
            f"0x{record['vif']:02X}"
        )

    print(
        f"     RAW Telegramm:   "
        f"{meter['raw_frame']}"
    )


def main():
    print("M-Bus Scan Primäradressen 1-10")
    print("--------------------------------")
    print(f"Port:       {PORT}")
    print("Baudrate:   2400")
    print("Format:     8E1")
    print(
        f"Timeout:    "
        f"{TIMEOUT_PER_METER} s pro Adresse"
    )
    print()

    ser = serial.Serial(
        PORT,
        baudrate=2400,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_EVEN,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.05
    )

    try:
        for address in range(
            START_ADDRESS,
            END_ADDRESS + 1
        ):
            scan_meter(ser, address)

            # Kleine Pause zwischen den Abfragen
            time.sleep(0.1)

    finally:
        ser.close()

    print()
    print("Scan beendet.")


if __name__ == "__main__":
    main()
