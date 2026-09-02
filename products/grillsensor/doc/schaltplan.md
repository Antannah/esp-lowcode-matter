# Schaltplan & Hardware-Dokumentation: Dual-Grillsensor (Seeed Studio XIAO ESP32-C6)

Dokumentation des analogen Frontends für den Dual-Grillsensor (IKEA Fantast / 100k NTC) am **Seeed Studio XIAO ESP32-C6** mit integriertem LiPo-Lader (800 mAh), Low-ESR Puffer-Elko gegen Thread-Sendespitzen (Brownout-Schutz), geschalteter Sensorversorgung (Power-Gating via GPIO), Tiefpass- und Bypass-Kondensatoren, Batteriespannungsüberwachung, Hardware-Ein/Aus-Schalter (Schubladen-Modus mit Ladefunktion) sowie Antennen-Umschaltung.

---

## 1. Schaltplan (Schematic)

```text
               D3 / GPIO 21 (SENSOR_PWR / V_SENSE_EN)
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
                 │ D0 / GPIO0    │               │ D1 / GPIO1    │
                 │ (ADC1_CH0)    │               │ (ADC1_CH1)    │
                 │               │               │               │
                 └───┬───────────┘               └───┬───────────┘
                     │                               │
                    GND                             GND

    ──────────────────────────────────────────────────────────────────
    Stromversorgung, Batteriemessung & Ein/Aus-Schalter:

             LiPo-Akku (800 mAh / 3.0V - 4.2V)
               │
             BAT+ (Lötpad Unterseite)
               │
               ├───┬───────────────────────────┐
               │   │                           │
              [R3] │                         + │
              1MΩ  │                        [C_bulk] 220µF - 470µF (Low-ESR Elko)
               │   │                         - │  (Puffert 2.4GHz Thread TX-Peaks)
               │   │                           │
               │   ├─────────── D2 / GPIO 2    │
               │   │            (ADC1_CH2 /    │
               │   │             BAT_SENSE)    │
               │   │                           │
              [R4] │                           │
              1MΩ [C3] 100nF                   │
               │   │ (Filter)                  │
               │   │                           │
             BAT- / GND────────────────────────┴───────────────────────
             (Gemeinsame Masse)

    ──────────────────────────────────────────────────────────────────
    Ein/Aus-Schalter (Schubladen-Ausschaltung & USB-Laden im Auszustand):

             EN / RST Pad (Signal-Kontakt des Reset-Tasters)
               │
               o
                \   [S1] Schiebeschalter (SPST / SPDT)
               o
               │
              GND (Gemeinsame Masse)

             * Schalter GESCHLOSSEN (EN -> GND): ESP32-C6 im Shutdown (~1 µA), Akku lädt bei USB-C
             * Schalter OFFEN (EN frei): Normalbetrieb / Messung & Matter-Datenübertragung
```

---

## 2. Pinbelegung: Seeed Studio XIAO ESP32-C6

| XIAO Pin | ESP32-C6 GPIO | Funktion im Grillsensor | ADC-Kanal / Typ | Beschreibung |
| :--- | :--- | :--- | :--- | :--- |
| **`D0`** | `GPIO 0` | **Grillfühler 1 (Kerntemperatur)** | `ADC1_CH0` (Analog In) | NTC-Spannungsteiler mit R1 ($47\text{ k}\Omega$) |
| **`D1`** | `GPIO 1` | **Grillfühler 2 (Garraum)** | `ADC1_CH1` (Analog In) | NTC-Spannungsteiler mit R2 ($47\text{ k}\Omega$) |
| **`D2`** | `GPIO 2` | **Batteriespannung ($V_{\text{BAT}}$)** | `ADC1_CH2` (Analog In) | Teiler 1:2 ($1\text{ M}\Omega / 1\text{ M}\Omega$) an BAT+ |
| **`D3`** | `GPIO 21` | **Sensor Power-Gating ($V_{\text{SENSE}}$)** | Digital Output | Versorgt R1/R2 nur während der Messung (High) |
| **`EN` / `RST`** | *(Pad/Taster)* | **Ein/Aus-Schalter (Power/Storage)** | Hardware Control | Schalter gegen GND: Shutdown / Schubladenmodus |
| *(intern)* | `GPIO 3` | **RF-Switch Enable** | Digital Output | Fest auf `LOW` (aktiviert HF-Schalter) |
| *(intern)* | `GPIO 14` | **Antennenauswahl** | Digital Output | `LOW` = Keramik / `HIGH` = Externe Antenne (U.FL) |
| **`BAT+`** | *(Pad unten)* | LiPo Pluspol (3.7V / 800 mAh) | Akku-Eingang | Integrierter USB-C Ladechip (SGM40567) |
| **`BAT-`** | *(Pad unten)* | LiPo Minuspol | GND | Gemeinsame Masse |
| **`GND`** | Pin 13 | System-Masse | GND | Masse für Klinkenbuchsen, Filter & Schalter |

---

## 3. Detail-Verdrahtung & Schutzbeschaltung

### Ein/Aus-Schalter (Schubladen-Aufbewahrung & Laden):
* **Schiebeschalter (S1):** Zwischen dem **`EN`/`RST`-Kontakt** (Signal-Seite des Onboard-Reset-Tasters) und **`GND`**.
  * **Schalter AUS (Position Schublade / Kontakt geschlossen):** Zieht `EN` dauerhaft auf `GND`. Der ESP32-C6 schaltet seine internen Spannungsregler komplett ab (**Hardware-Shutdown, Ruhestrom $\approx 1\text{–}2\,\mu\text{A}$**).
  * **USB-Ladefunktion im Auszustand:** Da der Akku fest an `BAT+`/`BAT-` angeschlossen bleibt, funktioniert das Aufladen über den USB-C Port auch im ausgeschalteten Zustand uneingeschränkt weiter.
  * **Schalter EIN (Position Betrieb / Kontakt offen):** Der interne Pull-Up-Widerstand bringt `EN` auf $3{,}3\text{ V}$, der Chip bootet sofort und verbindet sich mit dem Thread-Netzwerk.

### Stromversorgung & Brownout-Schutz (Low-ESR Elko):
* **$C_{\text{bulk}}$ ($220\text{ }\mu\text{F}\dots 470\text{ }\mu\text{F}$ Low-ESR Elko / Polymer / Tantal, $\ge 6{,}3\text{ V}$):**
  * Direkt parallel an **`BAT+`** und **`BAT-` / `GND`** angeschlossen.
  * **Zweck:** Fängt die kurzzeitigen Stromspitzen von $150\text{–}250\text{ mA}$ während der Thread-Funkübertragung ab.
  * **Vorteil:** Verhindert Spannungseinbrüche und Brownout-Resets, wenn der Akku unter $3{,}5\text{ V}$ entladen ist $\rightarrow$ Ermöglicht den stabilen Betrieb bis zur Entladeschlussspannung von $\approx 3{,}2\text{ V}$.

### Sensor-Stromversorgung (Power-Gating):
* **`D3` (`GPIO 21` / `SENSOR_PWR`):** Versorgt die Pull-Up-Widerstände ($R_1, R_2$) mit $3.3\text{ V}$.
  * Wird nur für die Messung auf `HIGH` geschaltet ($\approx 25\text{ ms}$ Einschwingzeit) und danach sofort wieder auf `LOW` (0 V) gesetzt.
  * Verhindert Ruhestromverluste über die NTC-Fühler gegen GND bei Inaktivität ($0\,\mu\text{A}$ Standby).

### Kanal 1 (Fühler 1 / Endpoint 1):
* **Pull-Up-Widerstand (R1):** $47\text{ k}\Omega$ zwischen `D3` (`GPIO 21`) und `D0` (`GPIO 0`)
* **Klinkenbuchse 1:**
  * Signal (Spitze/Tip) ➔ `D0` (`GPIO 0` / ADC1_CH0)
  * Masse (Schaft/Sleeve) ➔ `GND`
* **Entstörkondensator (C1):** $100\text{ nF}$ Keramik zwischen `D0` und `GND`

### Kanal 2 (Fühler 2 / Endpoint 2):
* **Pull-Up-Widerstand (R2):** $47\text{ k}\Omega$ zwischen `D3` (`GPIO 21`) und `D1` (`GPIO 1`)
* **Klinkenbuchse 2:**
  * Signal (Spitze/Tip) ➔ `D1` (`GPIO 1` / ADC1_CH1)
  * Masse (Schaft/Sleeve) ➔ `GND`
* **Entstörkondensator (C2):** $100\text{ nF}$ Keramik zwischen `D1` und `GND`

### Batteriespannungs-Erfassung (Endpoint 0 - Matter Power Source):
* **Spannungsteiler (R3, R4):** $2 \times 1\text{ M}\Omega$ zwischen `BAT+` und `GND` (Teilerfaktor 0.5, Dauerstrom $< 2{,}1\,\mu\text{A}$).
* **Mittelabgriff ➔ `D2` (`GPIO 2` / ADC1_CH2):**
  * $V_{\text{BAT}} = 4{,}2\text{ V} \longrightarrow V_{\text{ADC}} = 2{,}1\text{ V}$
  * $V_{\text{BAT}} = 3{,}7\text{ V} \longrightarrow V_{\text{ADC}} = 1{,}85\text{ V}$
  * $V_{\text{BAT}} = 3{,}0\text{ V} \longrightarrow V_{\text{ADC}} = 1{,}5\text{ V}$
* **Entstörkondensator (C3):** $100\text{ nF}$ Keramik zwischen `D2` und `GND`.

---

## 4. Antennenkonfiguration (XIAO ESP32-C6)

Das XIAO C6 Board verfügt über einen HF-Umschalter:
* **Interne Keramikantenne (Kompakt):**
  * `GPIO 3 = LOW`
  * `GPIO 14 = LOW`
* **Externe Stabantenne (U.FL Buchse / maximale Reichweite):**
  * `GPIO 3 = LOW`
  * `GPIO 14 = HIGH` (im Treiber standardmäßig aktiviert)

---

## 5. Stückliste (Bill of Materials)

| Pos | Bauteil | Wert / Spezifikation | Beschreibung |
| :--- | :--- | :--- | :--- |
| 1 | **MCU** | Seeed Studio XIAO ESP32-C6 | Matter-over-Thread Modul mit integriertem LiPo-Lader |
| 2 | **Akku** | 1S LiPo (3.7V / 800 mAh) | An BAT+ / BAT- Lötpads der Unterseite |
| 3 | **S1** | Mini-Schiebeschalter (SPST / SPDT) | Ein/Aus-Schalter an EN/RST gegen GND (Shutdown / Schubladenmodus) |
| 4 | **$C_{\text{bulk}}$** | $220\text{ }\mu\text{F}\dots 470\text{ }\mu\text{F}$ (Low-ESR Elko / Polymer, $\ge 6{,}3\text{ V}$) | Pufferkondensator an BAT+ / BAT- gegen Thread TX-Peaks |
| 5 | **R1, R2** | $47\text{ k}\Omega$ (1% Metallschicht) | Vorwiderstände für NTC-Spannungsteiler (an D3) |
| 6 | **R3, R4** | $1\text{ M}\Omega$ (1% Metallschicht) | Spannungsteiler für Akkumessung (an D2) |
| 7 | **C1, C2, C3** | $100\text{ nF}$ (0.1 µF, X7R Keramik) | Tiefpassfilter ($f_c \approx 33\text{ Hz}$) zur Rauschunterdrückung |
| 8 | **J1, J2** | 2.5 mm / 3.5 mm Klinkenbuchse (Mono/Stereo) | Buchsen für IKEA Fantast Fühler |

---

## 6. Mathematische Auslegung & Timing

### Tiefpassfilter-Grenzfrequenz:
$$f_c = \frac{1}{2 \pi \cdot R \cdot C} = \frac{1}{2 \pi \cdot 47000\,\Omega \cdot 100 \times 10^{-9}\,\text{F}} \approx 33{,}86\,\text{Hz}$$
* Unterdrückt $50\text{ Hz}$ / $100\text{ Hz}$ Netzbrummen und 2.4 GHz Einstreuungen auf langen Fühlerkabeln.

### RC-Einschwingzeit beim Power-Gating:
$$\tau = R \cdot C = 47\text{ k}\Omega \cdot 100\text{ nF} = 4{,}7\text{ ms}$$
* Vorlaufzeit von $25\text{ ms}$ nach dem Aktivieren von `D3` (`GPIO 21`) vor dem ADC-Read.

### Batteriespannungsberechnung:
$$V_{\text{BAT}} = V_{\text{ADC\_GPIO2}} \times \left(\frac{R_3 + R_4}{R_4}\right) = V_{\text{ADC\_GPIO2}} \times 2$$
* Der Messwert wird in Millivolt ($m\text{V}$) direkt in das Matter-Attribut `BatVoltage` (Cluster `0x002F`, Attribut `0x000B`) geschrieben und an Home Assistant übermittelt.
