# Änderung: M-Bus Zählerwert auf uint32_t umstellen

## Problem

In der aktuellen Firmware wird der M-Bus-Zählerwert als `int32_t` verarbeitet.

Bei einem Wasserzähler mit VIF `0x13` wird der eingegebene Wert in m³ vor dem Senden mit 1000 multipliziert.

Beispiel:

```text
Webwert: 123.456 m³
RAW-Wert: 123456
```

Dadurch ist der maximale darstellbare Wert nicht `4294967295 m³`, sondern bei 0,001 m³ Auflösung:

```text
4294967295 / 1000
= 4294967.295 m³
```

Zusätzlich sollte für Zählerstände kein signed `int32_t`, sondern ein unsigned `uint32_t` verwendet werden.

---

## Änderung im Quellcode

Suche im M-Bus-Code die Stelle, an der der RAW-Zählerwert erzeugt wird.

Aktuell ungefähr:

```cpp
int32_t raw = 0;

if (meter.unit == "kWh") {
    raw = static_cast<int32_t>(llround(meter.value));
} else {
    raw = static_cast<int32_t>(llround(meter.value * 1000.0));
}
```

Ersetze diesen Block durch:

```cpp
uint32_t raw = 0;

if (meter.unit == "kWh") {
    double v = meter.value;

    if (v < 0)
        v = 0;

    if (v > 4294967295.0)
        v = 4294967295.0;

    raw = static_cast<uint32_t>(llround(v));
} else {
    double v = meter.value * 1000.0;

    if (v < 0)
        v = 0;

    if (v > 4294967295.0)
        v = 4294967295.0;

    raw = static_cast<uint32_t>(llround(v));
}
```

---

## Maximalwerte

### Wasser / Gas mit 0,001 m³ Auflösung

```text
Maximaler Webwert:
4294967.295 m³

RAW:
4294967295

Hex:
FF FF FF FF
```

### kWh ohne zusätzliche Skalierung

Wenn der kWh-Wert direkt als RAW-Wert übertragen wird:

```text
Maximaler Webwert:
4294967295 kWh
```

---

## Erwarteter Test

In der Weboberfläche einstellen:

```text
Zählerwert:
4294967.295
```

Bei m³ sollte der Python-Parser danach anzeigen:

```text
Raw data:  FF FF FF FF
Raw value: 4294967295
Value:     4294967.295 m³
```

---

## Empfehlung für die Weboberfläche

Zusätzlich sollte später verhindert werden, dass zu große Werte eingegeben werden.

Für m³ bei 0,001 m³ Auflösung:

```text
max = 4294967.295
min = 0
```

Für direkt übertragene 32-Bit-Werte:

```text
max = 4294967295
min = 0
```

So kann kein Integer-Overflow mehr entstehen.
