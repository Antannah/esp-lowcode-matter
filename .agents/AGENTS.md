# Agent Guidelines & Context for esp-lowcode-matter

## Projekt-Kontext & Netzwerk
Dieses Repository enthält Firmware-Projekte auf Basis des **ESP-LowCode-Matter** Frameworks für ESP32 / ESP32-C6 Mikrocontroller.

### Smart Home Infrastruktur (Server: `tiffy`)
- **Matter Controller & Server:** Läuft auf `tiffy` (Port `5580` / UDP `5540` im Host-Netzwerk).
- **Home Assistant:** Hauptinstanz läuft als Docker-Container unter `home.antannah.blausee.eu`.
- **Thread Border Router:** Amazon Echo (Echo 4th Gen) bildet das Thread-Netzwerk für Matter-over-Thread Geräte.
- **Docker-Netzwerk Repository:** Das übergeordnete Server- & Traefik-Setup befindet sich im Nachbarverzeichnis `/home/norman/antigravity/traefik`.

---

## Entwicklungs- & Firmware-Regeln (Matter & Low-Code)

### 1. Datentypen & Matter Cluster
- **Temperatursensoren (Cluster `0x0402` / `1026`):**
  - Das Attribut `MeasuredValue` ist zwingend ein vorzeichenbehafteter 16-Bit Integer (`int16_t`) in Hundertstel Grad Celsius ($100 \times ^\circ\text{C}$, z. B. `2150` für `21,50 °C`).
  - Datentyp im LowCode-Framework: `LOW_CODE_VALUE_TYPE_INTEGER` mit `sizeof(int16_t)`. **Niemals `float` direkt übergeben!**
- **Periodische Updates:** Sensoren müssen ihre Werte über periodische Timer (`system_timer_create`) selbstständig an die Endpoints pushen.

### 2. Multi-Endpoint & Produkte
- Eigene Produkte liegen unter `products/<produktname>/` (z. B. `products/grillsensor/`).
- Jedes Produkt enthält seine eigenen ZAP-Dateien (`data_model_thread.zap` / `data_model_wifi.zap`) und C++ Quellcodes in `main/`.

### 3. Andrej Karpathy Coding Guidelines
- **Think Before Coding:** Keine Annahmen treffen, Trade-offs transparent darlegen.
- **Simplicity First:** Minimaler, wartbarer C/C++ Code ohne unnötige Abstraktionen.
- **Surgical Changes:** Nur modifizieren, was zur konkreten Aufgabe gehört.
