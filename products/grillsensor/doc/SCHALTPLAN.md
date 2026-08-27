# Schaltplan & Hardware-Dokumentation: Dual-Grillsensor (ESP32-C6)

Dokumentation des analogen Frontends für den Dual-Grillsensor (IKEA Fantast / 100k NTC) am **ESP32-C6** mit Tiefpass- und Bypass-Kondensatoren.

---

## 1. Schaltplan (Schematic)

```text
               +3.3V (ESP32-C6)
                 │
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
```

---

## 2. Pinbelegung & Verdrahtung

### Kanal 1 (Fühler 1 / Endpoint 1):
* **Pull-Up-Widerstand (R1):** $47\text{ k}\Omega$ zwischen `3.3V` und `GPIO 0` (ADC1_CH0)
* **Klinkenbuchse 1:**
  * Signal (Spitze/Tip) ➔ `GPIO 0`
  * Masse (Schaft/Sleeve) ➔ `GND`
* **Entstörkondensator (C1):** $100\text{ nF}$ Keramik zwischen `GPIO 0` und `GND`

### Kanal 2 (Fühler 2 / Endpoint 2):
* **Pull-Up-Widerstand (R2):** $47\text{ k}\Omega$ zwischen `3.3V` und `GPIO 1` (ADC1_CH1)
* **Klinkenbuchse 2:**
  * Signal (Spitze/Tip) ➔ `GPIO 1`
  * Masse (Schaft/Sleeve) ➔ `GND`
* **Entstörkondensator (C2):** $100\text{ nF}$ Keramik zwischen `GPIO 1` und `GND`

---

## 3. Spannungsversorgungs-Filter (Power Decoupling)

Um Spannungseinbrüche während der 2.4 GHz Thread/Mesh-Sendespitzen abzufedern:
* **$C_{\text{dec}}$ (100 nF Keramik):** Direkt zwischen `3.3V` und `GND` am ESP32-C6 Modul.
* **$C_{\text{bulk}}$ (10 µF – 100 µF Elektrolyt / Tantal):** Parallel dazu an der Versorgungsspannung.

---

## 4. Stückliste (Bill of Materials)

| Pos | Bauteil | Wert / Spezifikation | Beschreibung |
| :--- | :--- | :--- | :--- |
| 1 | **R1, R2** | $47\text{ k}\Omega$ (1% Metallschicht) | Vorwiderstände für den Spannungsteiler |
| 2 | **C1, C2** | $100\text{ nF}$ (0.1 µF, X7R Keramik) | Tiefpassfilter ($f_c \approx 33\text{ Hz}$) zur Rauschunterdrückung |
| 3 | **J1, J2** | 2.5 mm / 3.5 mm Klinkenbuchse (Mono/Stereo) | Buchsen für Grillfühler |
| 4 | **MCU** | ESP32-C6 Entwicklungsboard | Matter-over-Thread Mikrocontroller |

---

## 5. Mathematische Auslegung (Filter & Formel)

### Tiefpassfilter-Grenzfrequenz:
$$f_c = \frac{1}{2 \pi \cdot R \cdot C} = \frac{1}{2 \pi \cdot 47000\,\Omega \cdot 100 \times 10^{-9}\,\text{F}} \approx 33{,}86\,\text{Hz}$$

* Unterdrückt $50\text{ Hz}$ / $100\text{ Hz}$ Netzbrummen und hochfrequente Einstreuungen der Thread-Funkantenne wirksam auf den langen Fühlerkabeln.
