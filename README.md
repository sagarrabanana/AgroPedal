# AgroPedal

La pedalera-MIDI DIY oficial de LETRINA Punk & Bass, y de quien quiera replicarla.
Con 16 bancos 3x16 pulsadores y dos poteciometros (conectados via jack stereo), para interactual en directo con tu DAW favorito de una forma mas vistosa que con un MPKmini.

![agropedal_v1](https://raw.githubusercontent.com/sagarrabanana/AgroPedal/refs/heads/main/pedal.png)
![letrina_v1](https://raw.githubusercontent.com/sagarrabanana/AgroPedal/refs/heads/main/letrina_aro.png)

# Esquema
```
┌─────────────────────────────────┐
│                                 │
│   USB                           │
│   [===]                         │
│                                 │
│ GND  ─────┬───── GND común para │
│ 3.3V ─────┼───── pots y botones │
│           │                     │
│ GPIO 4  ──┴──── Pulsador P1     │
│ GPIO 5  ─────── Pulsador P2     │
│ GPIO 6  ─────── Pulsador P3     │
│                                 │
│ GPIO 9  ─────── Pot J1 (wiper)  │
│ GPIO 8  ─────── Pot J2 (wiper)  │
│                                 │
│ GPIO 38 ─────── LED0 + R(220Ω) ─┴─ GND
│ GPIO 39 ─────── LED1 + R(220Ω) ─┴─ GND
│ GPIO 40 ─────── LED2 + R(220Ω) ─┴─ GND
│ GPIO 41 ─────── LED3 + R(220Ω) ─┴─ GND
└─────────────────────────────────┘

Pots: Conecta el pin central (wiper) al GPIO, los extremos a 3.3V y GND.

Pulsadores: Usa INPUT_PULLUP interno, así que solo necesitas conectar el otro lado a GND

LEDs: SIEMPRE con resistencia limitadora (220Ω-330Ω) en serie
```
