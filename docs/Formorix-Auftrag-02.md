# Formorix Slicer – Auftrag 02

Stand: 9. November 2026. Entwicklungsbranch: `formorix/auftrag-02`, aufbauend auf
`formorix/auftrag-01`. Dieser Auftrag ergänzt die vorhandene Orca-GUI um die
Bearbeitung von 5D-Oberflächenvorgaben. Er enthält keine 5D-Druckalgorithmen.

## Umgesetzt

- Neuer Tab **5D-Oberflächen** mit Bereichsliste links, dem vorhandenen
  Orca-3D-Viewport in der Mitte und aufklappbaren Einstellungsgruppen rechts.
- Tatsächliche zusammenhängende Flächenauswahl, Pinselmarkierung und Entfernen,
  Neigungsauswahl, Kantengrenze, Referenzkante und Schnittansicht. Markierungen
  besitzen eigene Farben und beeinflussen normales Material-/Support-Painting
  nicht. Die bestehende Kamerabedienung und Undo/Redo werden wiederverwendet.
- Bereiche benennen, hinzufügen, entfernen, zusammenfassen, Einstellungen
  übertragen und Markierungen ausblenden. Zusammenfassen speichert den Wunsch
  nach durchgehenden oberflächenübergreifenden Bahnen; die Beschränkung auf einen
  Kopf ist standardmäßig aktiv.
- Schichtanzahl, Schalendicke entlang der lokalen Normalen oder vollständige
  Wandstärke; eigene 5D-Schichtdicke und standardmäßig erhaltene Modellabmessungen.
  Bei vollständiger Wand erscheint der geforderte Tragstrukturhinweis.
- Normale Ausrichtung, erlaubte Abweichung bis 15°, senkrechte Flächen und ein
  ausdrücklicher Hinweis auf die noch ausstehende Erreichbarkeitsprüfung.
- Unterbauwünsche und direkt eingebundene Orca-Support-Einstellungen, einschließlich
  Interface, Mustern, Abständen, Materialzuordnung und Infill first. Diese Felder
  bearbeiten dieselbe Prozesskonfiguration wie der normale Orca-Dialog.
- Feste Grenze, verwobener Übergang und parallele Bahnen als gespeicherte
  Fertigungsvorgaben. Breite, Überlappungsgrad und Muster sind einstellbar;
  Nachbarschaft und Materialverträglichkeit bleiben als ungeprüft gekennzeichnet.
- Drei aktivierbare Köpfe mit Verweisen auf vorhandene Orca-Filamentprofile,
  mehreren Aufgaben je Kopf und einer Anzeige zugeordneter Bereiche. Strategien:
  automatischer Parallelbetrieb, materialabhängige Aufgaben und High-Speed.
  Die High-Speed-Vorprüfung berücksichtigt PETG, Profilkompatibilität,
  Aktivierung und überlappende Temperaturbereiche; sie ist keine technische Freigabe.
- Oberflächenrichtung, wechselnde Richtungen, feinere letzte Schichten,
  Grenzabstand, Qualität und zukünftiges 5D-Ironing. Geschwindigkeit, Linienbreite,
  Flow, Temperatur und Kühlung bleiben aus Orca-Profilen übernommen.
- Versionierte Formorix-Daten innerhalb normaler 3MF-Projekte, einschließlich
  fein unterteilter Pinselmasken. Speichern übernimmt auch das noch fokussierte
  Zahlen- oder Namensfeld. Unbekannte Datenversionen werden unverändert erhalten
  und im Editor gesperrt.
- Deutsche Beschriftungen, Architekturübersicht und gezielte automatisierte Tests.

Die Architektur und die Dateiformatgrenzen stehen in
[HLSD/formorix-surfaces.md](HLSD/formorix-surfaces.md).

## Build und automatisierte Prüfungen

Geprüft wurde Windows x64 mit Visual Studio 2026 Community, MSVC 14.51,
Windows SDK 10.0.26100 und CMake 4.4.3. Die bereits eingerichteten Orca-Abhängigkeiten
wurden wiederverwendet.

| Prüfung | Ergebnis |
| --- | --- |
| Release-Build einschließlich Tests und lokaler Installation | Erfolgreich |
| Abschließender GUI-Build und lokale Installation | Erfolgreich |
| CTest, ohne `NotWorking` und `RequiresApp` | 3’084 bestanden, 6 übersprungen, 0 fehlgeschlagen; 131,13 s |
| Neue Kernprüfungen `[FormorixSurfaces]` | 7 Testfälle, 66 Assertions bestanden |
| Druck-/Exportprüfungen `[FormorixSurfaces],[Formorix5D]` | 4 Testfälle, 19 Assertions bestanden |
| clang-tidy auf den 20 geänderten C++-Dateien | Bestanden |
| Vollständiger gettext-Lauf und deutsche Format-/Headerprüfung | Erfolgreich |
| Deutscher Übersetzungscheck | 0 Fehler, 62 Warnungen im gesamten AI-markierten Katalogumfang |

Die sechs übersprungenen Fälle benötigen Windows-Symlink-Rechte. Bereits als
`NotWorking` beziehungsweise `RequiresApp` markierte Tests wurden ausgeschlossen;
die Linux-spezifische CLI-Suite ist auf Windows nicht registriert.

Die neuen Tests prüfen alle gespeicherten Einstellungsfelder, ungültige Werte,
Zusammenfassen/Entfernen, native unterteilte Dreiecksmasken, tatsächliches
3MF-Schreiben und -Lesen, unbekannte Datenversionen, geänderte Netztopologie und
Modellkopien. Ein Drucktest vergleicht normale planare G-Code-Befehle mit und ohne
5D-Annotationen. Die bisherige Ablehnung des experimentellen Druckmodus wird
ebenfalls geprüft.

Der Linker meldet weiterhin `LNK4098` zur gemischten CRT-Verwendung. Ein isolierter
Drucktest meldet die fehlende `info/nozzle_info.json` im Test-Ressourcenpfad;
seine Assertions bestehen. Beides wurde nicht durch zusätzliche Änderungen an
der allgemeinen Build-Konfiguration kaschiert.

## Prüfung im laufenden Fenster

Verwendet wurden ein eigens erzeugter 20-mm-Würfel, ein isoliertes Test-Datenverzeichnis,
das Formorix-5D-Profil und die deutsche Oberfläche. Tatsächlich ausgeführt:

1. Programm starten, zwischen der normalen Vorbereitung und dem neuen Tab wechseln.
2. Zwei Modellflächen anklicken, unterschiedliche Bereichsfarben kontrollieren
   und beide Bereiche zu einem durchgehenden Bereich zusammenfassen.
3. Schichtanzahl auf 7 ändern; Projekt speichern, Anwendung schließen und erneut
   öffnen. Einstellungen und farbige Markierungen bleiben vorhanden.
4. Zweiten Bereich anlegen und einen echten Pinselstrich aufbringen. Undo entfernt
   den Strich, Redo stellt ihn wieder her.
5. Die vorhandene Orca-Support-Aktivierung innerhalb des 5D-Tabs ändern und den
   gespeicherten gemeinsamen Prozesswert `enable_support=1` prüfen.
6. Kopf 1 dem Profil `Generic PETG @System` und gleichzeitig Bauteilaufbau und
   5D-Oberflächen zuordnen; Kopf 2 Stützen, Kopf 3 Verstärkung zuweisen.
   Materialabhängige Aufgabenstrategie wählen und gespeicherte Werte prüfen.
7. Die drei Übergangsarten anzeigen und einen verwobenen Übergang auswählen.
   Nach einem Neustart 3,5 mm wiederfinden; anschließend direkt aus dem aktiven
   Eingabefeld `4,25` mit Strg+S speichern und 4,25 mm im 3MF-JSON nachweisen.
8. Schnittposition auf 0,5 stellen: Das Originalmodell wird sichtbar geschnitten.
   Anschließend die Schnittansicht zurücksetzen.
9. Die Neigungsauswahl prüfen. Die direkte Auswahl muss auch die farbigen
   Render-Patches erneuern; dieser Aktualisierungsweg ist entsprechend korrigiert.
   Im abschließenden Build färbt die Auswahl von 0–180° den Würfel unmittelbar
   in der Farbe des aktiven Bereichs; Undo stellt den vorherigen Zustand wieder her.
10. Eine Referenzkante im Modell anklicken und die gespeicherten Endpunkte sowie
    die Richtungsvorgabe im 3MF prüfen. Der Editor bestätigt die Auswahl und
    kennzeichnet die noch nicht berechnete Bahnausrichtung.

Das Projekt wurde zusätzlich als ZIP geprüft: Es enthält Formorix-Einstellungen
und Dreiecksmarkierungen, aber keine `.gcode`- oder `.bgcode`-Datei. Beim Start mit
aktivem experimentellem Profil erscheint weiterhin die bestehende Export-/Slicing-Sperre.

## Screenshots und Beispiel

Die Bilder stammen aus dem tatsächlich laufenden Windows-Build. Auf dem
verfügbaren Desktop mit etwa 900 Pixeln Fensterbreite sind die Seitenbereiche
vertikal scrollbar; zusätzliche Gruppen sind in eigenen Aufnahmen zu sehen.

- [Vollständiger Oberflächeneditor](images/formorix-surfaces/surface-editor.jpg)
- [Pinselmarkierung und verwobener Übergang](images/formorix-surfaces/brush-transition.jpg)
- [Mehrkopf-Betrieb und PETG-Prüfstatus](images/formorix-surfaces/multi-head.jpg)
- [Vorhandene Orca-Support-Einstellungen im 5D-Tab](images/formorix-surfaces/orca-supports.jpg)
- [Schnittansicht des Originalmodells](images/formorix-surfaces/section-view.jpg)
- [Sichtbare Neigungsauswahl](images/formorix-surfaces/slope-selection.jpg)
- [Normaler Orca-Viewport vor dieser Erweiterung, Auftrag 01](images/formorix/planar-slicing.jpg)

[Beispielprojekt mit zwei Bereichen](examples/formorix-surfaces.3mf): als Projekt
öffnen, den Tab **5D-Oberflächen** wählen und die Kamera auf das Modell einpassen.
Das Beispiel enthält den verwobenen Übergang mit 4,25 mm, die Kopfaufgaben,
eine gespeicherte Referenzkante und den experimentellen Druckmodus mit aktiver Sperre.

## Grenzen und offene Abnahme

- Windows wurde gebaut und bedient. macOS, Linux, andere DPI-Stufen und die
  vollständige Kombination aller GUI-Einstellungen wurden nicht praktisch geprüft.
- Die positive High-Speed-Materialkombination und alle einzelnen Auswahl-/Eingabe-
  Kombinationen benötigen ergänzende manuelle Abnahme. Der negative Status bei
  fehlenden Kopfmaterialien wurde im Fenster überprüft.
- Höchstens 32 Bereiche sind möglich; die acht Farben wiederholen sich danach.
  Die Referenzkante ist eine gespeicherte Netzkante, keine fortlaufende CAD-Bindung.
- Die Schnittansicht zeigt das vorhandene Modell. Es gibt keine berechnete
  5D-Schale oder Vorschau ihrer tatsächlichen Wandstärke.
- Kinematik, Kollisionsfreiheit, Materialhaftung, Nachbarschaft, durchgehende oder
  verwobene Bahnen, parallele Arbeitsverteilung und Druckzeit sind nicht berechnet.
- Fehlende Filamentprofile bleiben als ungelöste Referenzen erhalten. Andere
  Slicer können Formorix-Erweiterungsdaten beim erneuten Speichern verwerfen.
- Es gibt keine ausführbaren 5D-Druckdateien, keine Klipper-Ansteuerung und keine
  Änderungen an der bisherigen experimentellen Exportsperre. Die normale
  planare Pipeline und vorhandene Druckerprofile bleiben erhalten.
- Das bestehende Repository `Lenticularis0/Formorix` wurde nicht verändert.

Quellcode, Übersetzungen, Tests, Dokumentation, Screenshots und Beispielprojekt gehören zum
Entwicklungsbranch. Build-Verzeichnisse, lokale Testdaten und große Logdateien
bleiben lokal. Die installierte Anwendung liegt unter
`build/OrcaSlicer/orca-slicer.exe`; der interne Dateiname bleibt aus
Kompatibilitätsgründen erhalten.
