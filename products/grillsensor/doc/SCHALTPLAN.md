# Schaltplan & Hardware-Dokumentation: Dual-Grillsensor (ESP32-C6)

Dokumentation des analogen Frontends für den Dual-Grillsensor (IKEA Fantast / 100k NTC) am **ESP32-C6** mit geschalteter Sensorversorgung (Power-Gating via GPIO), Tiefpass- und Bypass-Kondensatoren sowie integrierter **Batteriespannungsüberwachung (Power Source)**.

---

## 1. Schaltplan (Schematic)

```text
               GPIO 2 (SENSOR_PWR / V_SENSE_EN)
                 │  (High nur während der ADC-Messung)
                 ├───┬───────────────────────────┐
                 │   │                           │
                [R1] │                          [R2]
               47kΩ  │                         47kΩ  
                 │   │                           │   
                 ├───┼───────────┐               ├───┼───────────┐
                 │   │           │               │   │           │
                 ▼   │           │               ▼   │           │
           [Klinke 1]│          [C1]       [Klinke 2]│          [C2]
           Fantast-  │         100nF       Fantast-  │         100nF
           Fühler 1  │      (Keramik)      Fühler 2  │      (Keramik)
           (NTC)     │           │         (NTC)     │           │
                 │   │           │               │   │           │
                 │   ▼           │               │   ▼           │
                 │ GPIO0         │               │ GPIO1         │
                 │ (ADC1_CH0)    │               │ (ADC1_CH1)    │
                 │               │               │               │
                 └───┬───────────┘               └───┬───────────┘
                     │                               │
                    GND                             GND

            ──────────────────────────────────────────────────────
            Batteriespannungsmessung (Li-Ion / V_BAT -> GPIO 3):

                     V_BAT (3.0V - 4.2V Akku)
                       │
                      [R3] 1MΩ
                       │
                       ├─────────────── GPIO 3 (ADC1_CH3 / BAT_SENSE)
                       │           │
                      [R4]        [C3] 100nF (Filter)
                      1MΩ          │
                       │           │
                      GND──────────┘
```

---

## 2. Pinbelegung & Verdrahtung

### Sensor-Stromversorgung (Power-Gating):
* **GPIO 2 (`SENSOR_PWR`):** Versorgt die Pull-Up-Widerstände ($R_1, R_2$) mit $3.3\text{ V}$.
  * Wird nur für die Messung auf `HIGH` geschaltet ($\approx 25\text{ ms}$ Einschwingzeit) und danach sofort wieder auf `LOW` (0 V) gesetzt.
  * Verhindert Ruhestromverluste über die NTC-Fühler gegen GND bei Inaktivität.

### Kanal 1 (Fühler 1 / Endpoint 1):
* **Pull-Up-Widerstand (R1):** $47\text{ k}\Omega$ zwischen `GPIO 2` (`SENSOR_PWR`) und `GPIO 0` (ADC1_CH0)
* **Klinkenbuchse 1:**
  * Signal (Spitze/Tip) ➔ `GPIO 0`
  * Masse (Schaft/Sleeve) ➔ `GND`
* **Entstörkondensator (C1):** $100\text{ nF}$ Keramik zwischen `GPIO 0` und `GND`

### Kanal 2 (Fühler 2 / Endpoint 2):
* **Pull-Up-Widerstand (R2):** $47\text{ k}\Omega$ zwischen `GPIO 2` (`SENSOR_PWR`) und `GPIO 1` (ADC1_CH1)
* **Klinkenbuchse 2:**
  * Signal (Spitze/Tip) ➔ `GPIO 1`
  * Masse (Schaft/Sleeve) ➔ `GND`
* **Entstörkondensator (C2):** $100\text{ nF}$ Keramik zwischen `GPIO 1` und `GND`

### Batteriespannungs-Erfassung (Endpoint 0 - Matter Power Source):
* **Spannungsteiler (R3, R4):** $2 \times 1\text{ M}\Omega$ zwischen $V_{\text{BAT}}$ und `GND` (Teilerfaktor 0.5, Ruhestrom $< 2{,}1\,\mu\text{A}$).
* **Mittelabgriff ➔ `GPIO 3` (ADC1_CH3):**
  * $V_{\text{BAT}} = 4{,}2\text{ V} \longrightarrow V_{\text{ADC}} = 2{,}1\text{ V}$
  * $V_{\text{BAT}} = 3{,}7\text{ V} \longrightarrow V_{\text{ADC}} = 1{,}85\text{ V}$
  * $V_{\text{BAT}} = 3{,}0\text{ V} \longrightarrow V_{\text{ADC}} = 1{,}5\text{ V}$
* **Entstörkondensator (C3):** $100\text{ nF}$ Keramik zwischen `GPIO 3` und `GND`.

---

## 3. Spannungsversorgungs-Filter (Power Decoupling)

Um Spannungseinbrüche während der 2.4 GHz Thread/Mesh-Sendespitzen abzufedern:
* **$C_{\text{dec}}$ (100 nF Keramik):** Direkt zwischen `3.3V` und `GND` am ESP32-C6 Modul.
* **$C_{\text{bulk}}$ (10 µF – 100 µF Elektrolyt / Tantal):** Parallel dazu an der Versorgungsspannung.

---

## 4. Stückliste (Bill of Materials)

| Pos | Bauteil | Wert / Spezifikation | Beschreibung |
| :--- | :--- | :--- | :--- |
| 1 | **R1, R2** | $47\text{ k}\Omega$ (1% Metallschicht) | Vorwiderstände für den NTC-Spannungsteiler (an GPIO 2) |
| 2 | **R3, R4** | $1\text{ M}\Omega$ (1% Metallschicht) | Hochohmiger Spannungsteiler für Akkumessung |
| 3 | **C1, C2, C3** | $100\text{ nF}$ (0.1 µF, X7R Keramik) | Tiefpassfilter ($f_c \approx 33\text{ Hz}$) zur Rauschunterdrückung |
| 4 | **J1, J2** | 2.5 mm / 3.5 mm Klinkenbuchse (Mono/Stereo) | Buchsen für Grillfühler |
| 5 | **MCU** | ESP32-C6 Entwicklungsboard | Matter-over-Thread Mikrocontroller |

---

## 5. Mathematische Auslegung & Timing

### Tiefpassfilter-Grenzfrequenz:
$$f_c = \frac{1}{2 \pi \cdot R \cdot C} = \frac{1}{2 \pi \cdot 47000\,\Omega \cdot 100 \times 10^{-9}\,\text{F}} \approx 33{,}86\,\text{Hz}$$

* Unterdrückt $50\text{ Hz}$ / $100\text{ Hz}$ Netzbrummen und hochfrequente Einstreuungen der Thread-Funkantenne wirksam auf den langen Fühlerkabeln.

### RC-Einschwingzeit beim Power-Gating:
$$\tau = R \cdot C = 47\text{ k}\Omega \cdot 100\text{ nF} = 4{,}7\text{ ms}$$
* Vorlaufzeit von $25\text{ ms}$ nach dem Aktivieren von `GPIO 2` vor dem ADC-Read.

### Batteriespannungsberechnung:
$$V_{\text{BAT}} = V_{\text{ADC\_GPIO3}} \times \left(\frac{R_3 + R_4}{R_4}\right) = V_{\text{ADC\_GPIO3}} \times 2$$
* Der Messwert wird in Millivolt ($m\text{V}$) direkt in das Matter-Attribut `BatVoltage` (Cluster `0x002F`, Attribut `0x000B`) geschrieben und an Home Assistant übermittelt.
