# Stromkosten-Display

Zeigt auf einer 8x8-Matrix aus WS2812-LEDs die Strompreise der
nächsten Stunden und den Status des Stromnetzes an. 

![](./display.jpg)

Primär interessant für österreichische Haushalte mit dynamischen
Stromtarifen, die Idee ist, die Preise im Blick zu haben, bevor man
Großverbraucher einschaltet.

Die Preise kommen über die
[aWATTar-API](https://www.awattar.at/services/api/), der Status von
[APG](https://www.apg.at/powermonitor/#c3883). 

## Ablesen

Der linke senkrechte Balken ist der Preis der aktuellen vollen Stunde,
nach rechts sind dann die folgenden Stunden.

Die Länge und Farbe der Balken gibt den Preis an, grün bis 8¢/kWh,
gelb bis 12¢/kWh, rot darüber, und blau für negative Preise.

Der horizontale Balken ganz oben ist der Status des Stromnetzes gemäß
[APG Powermonitor](https://www.apg.at/powermonitor/), passend zu den
stundenweisen Preisbalken darunter. Grün ist OK, Rot ist Spitzenlast.

## Hardware

* Ein ESP32, das Gehäuse ist für
  [diesen](https://www.az-delivery.de/products/esp32-dev-kit-c-unverlotet?variant=32437204549728).
  Tunlichst einen ohne Pin-Header nehmen, im Gehäuse ist dafür kein Platz.
* eine 8x8-Matrix aus WS2812-RGB-LEDs, wie [diese](https://www.az-delivery.de/products/u-64-led-panel?variant=40362432466)
* Ein 3D-gedrucktes Gehäuse, siehe Directory [CAD](./CAD/)
  
### Aufbau

5V und GND der LED-Matrix verbinden, GPIO16 vom ESP (`LED_PIN` im Code) mit
dem Datenpin der Matrix, fertig.

Evtl zuerst mal [WLED](https://install.wled.me/) draufflashen, das
geht einfach und man kann die Funktion der Hardware damit schön und
bunt prüfen.


### Gehäuse

[basis.stl](CAD/basis.stl) in einer undurchsichtigen Farbe,
[abdeckung.stl](CAD/abdeckung.stl) in transparentem oder weißem
Filament drucken.

Die LED-Matrix von hinten in die Basis klemmen (oder mit Heißkleber
nachhelfen), Pixel #1 kommt nach links oben. Das ESP-board einfach
dahinterlegen, sinnvollerweise mit einer Isolationsschicht dazwischen.
Mit 3mm-Spaxen an die Wand, Abdeckung drauf, fertig.

## Installation

Software-Installation: basiert auf
[PlatformIO](https://platformio.org/).

1) `include/config.h.dist` auf `include/config.h` kopieren und
editieren (WiFi-Daten, Schwellwerte für die Display-Farben)

2) `platformio.ini` ggf anpassen in sachen Upload

3) `pio run -t upload`

## Security-Warnung

Die APIs sind über HTTPS erreichbar. TL;DR: *die
Zertifikatsverifikation ist im Code ausgeschaltet.*

Da die HTTPS-Libraries für ESP32 nur ein einziges Root-Cert
unterstützen, besteht bei korrekter Verwendung das Problem, daß man
bei Änderung einer der CAs der API-Server dies erst mitbekommt, wenn
es schon geschehen ist, dann das korrekte Cert finden und in den Code
integrieren muß (oder mit noch mehr Programmieraufwand das im
Filesystem speichern und einen Mechanismus zum Updaten ohne
Recompilieren vorsehen muß). 

Das war mir zu aufwendig und widerspricht der Idee "Display, das
einfach an der Wand hängt und funktioniert."
