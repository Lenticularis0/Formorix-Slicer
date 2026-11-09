# Formorix Slicer – Auftrag 01

Stand: 9. November 2026. Dieser Bericht beschreibt die tatsächlich ausgeführte
Windows-Entwicklung und Prüfung. Auftrag 02 wurde nicht begonnen.

## Übernommene Grundlage

- Echter GitHub-Fork: <https://github.com/Lenticularis0/Formorix-Slicer>.
- Vollständiger, nicht flacher Checkout einschließlich Historie und Tags.
- Ausgangspunkt: OrcaSlicer `main`, Commit
  `f55615d7488fe570942f5e25548b8ab9b30778d0`, Version `2.6.0-dev`.
- Entwicklungsbranch: `formorix/auftrag-01`.
- `origin` zeigt auf den Formorix-Fork, `upstream` auf
  <https://github.com/OrcaSlicer/OrcaSlicer>.
- `LICENSE.txt`, erforderliche Fremdlizenzen und ursprüngliche Credits bleiben
  erhalten. Formorix-Ergänzungen stehen unter AGPL-3.0-or-later.
- Das separate CAD-Repository `Lenticularis0/Formorix` wurde nicht verändert.

## Umsetzung

Die vorhandene C++-Engine und wxWidgets-Oberfläche bleiben die Grundlage.
Anwendungsname, Fenstertitel und Begrüßung lauten **Formorix Slicer**. Der
About-Dialog nennt OrcaSlicer als Grundlage und verlinkt den Quellcode des Forks.
Icons und bestehendes Layout bleiben erhalten. Der separate Anwendungsschlüssel
`FormorixSlicer` vermeidet eine Vermischung mit vorhandenen Orca-Einstellungen.
Die Anwendung bietet keine Orca-Programmdatei als Update für den Fork an;
Programmupdates verweisen auf das Formorix-Repository. Profilupdates bleiben erhalten.

Das zusätzliche Herstellerpaket **Formorix** enthält das Modell **Formorix 5D**,
das Maschinenprofil **Formorix 5D 0.4 nozzle** und ein passendes Standardprozessprofil.
Die bestehenden Herstellerprofile wurden nicht verändert.

| Parameter | Voreinstellung |
| --- | --- |
| Bauraum | 1600 × 350 × 350 mm |
| Unabhängige Köpfe | 3 |
| Koordinaten je Kopf | X, Y, lokales Z, A, B |
| Drehachsengrenzen | −75° bis +75° je Achse |
| Lokaler Kopf-Z-Hub | 100 mm |
| Gemeinsamer Bett-Z-Hub | 350 mm |
| Zielgeschwindigkeit | 500 mm/s |
| Zielbeschleunigung | 10000 mm/s² |
| Logische Extrusionskanäle | 0, 1, 2 |

Die neue Seite **Formorix 5D** im bestehenden Druckereinstellungsdialog enthält
den Modusschalter, globale Grenzen und drei getrennte Kopfgruppen. Die Werte
verwenden Orcas vorhandene Bearbeitung, Änderungserkennung und Speicherung.
56 skalare Konfigurationsfelder einschließlich Schemaversion sind registriert;
die Schemaversion wird nicht als normale Benutzereinstellung angeboten.
Die Kopfkonfiguration ist unabhängig von Orcas herkömmlichen Extruderarrays.

Das Modul `src/libslic3r/Formorix5D.{hpp,cpp}` enthält Datenmodell,
Konfigurationsdefinitionen, Validierung und die gemeinsame Ausgabesperre.
Die Integration und ihre Grenzen sind in der
[Architekturbeschreibung](HLSD/formorix-5d.md) dokumentiert.

Experimenteller Modus blockiert Slicing, ausführbaren G-Code, Druckarchive und
Sendewege. Editierbare 3MF-Projekte und Sicherungen bleiben möglich und enthalten
keine ausführbaren Bahndaten. Ein Moduswechsel verwirft zwischengespeicherte
Slicing-Ergebnisse. Im normalen Modus bleiben die bisherigen Slicing-Algorithmen
unverändert; alte Orca-G-Code-Kopfzeilen werden weiterhin erkannt.

## Build und Prüfungen

Entwicklungsplattform: Windows x64, Visual Studio Community 2026 / MSVC
14.51.36231, Windows SDK 10.0.26100, CMake 4.4.3, Git 2.55 und Strawberry Perl.
Die ursprünglichen optionalen Funktionen wurden beim Build nicht abgeschaltet.
Die Abhängigkeiten einschließlich wxWidgets, OpenCASCADE, OpenVDB, OpenCV und
Python wurden mit dem vorhandenen Buildskript gebaut.

| Prüfung | Ergebnis |
| --- | --- |
| Abhängigkeiten: `build_win.bat -d --msvc --msbuild -j 8` | Erfolgreich, 27 min 21 s |
| Abschließender Release-Build mit Tests und Installation | Erfolgreich, letzter inkrementeller Lauf 2 min 54 s |
| CTest, sechs vorhandene Testsuiten | 3082 ausgewählt; 3076 bestanden, 6 übersprungen, 0 Fehler; 130,43 s |
| Neue Formorix-Tests | 10 Testfälle, 333 Assertions bestanden |
| Clang-Tidy gegen `upstream/main` | Alle 24 betroffenen C++-Dateien bestanden |
| Profilprüfung | 73 Herstellerpakete, 0 Fehler und 0 Warnungen |
| Profil-Versionsprüfung | Bestanden |
| `scripts/run_gettext.bat --full` | Bestanden |
| `git diff --check` | Bestanden |

CTest lief mit `-LE 'NotWorking|RequiresApp'`, entsprechend der vorhandenen
Unit-Test-Auswahl. Die sechs übersprungenen Fälle benötigen Windows-Symlink-Rechte,
die der Testprozess nicht hatte. Die Linux-CLI-Suite ist unter Windows nicht
registriert. NumPy 2.5.3 wurde nur in den isolierten Python-Testinterpreter installiert.
Die [Entwicklungsanleitung](Formorix-development.md) enthält reproduzierbare Befehle.

Während der Einrichtung wurden folgende Probleme behoben:

- Die Registrierung neuer Optionen erfolgt jetzt über die besitzende
  Konfigurationsdefinition; der erste Versuch verletzte deren geschützte API.
- Direkte Includes wurden nach der Clang-Tidy-Prüfung ergänzt.
- Nach dem Branding schützt die Plugin-Prüfung sowohl Formorix- als auch alte
  Orca-Konfigurationsdateinamen. Der zunächst fehlgeschlagene Bestandstest und
  seine zusätzlichen Formorix-Fälle bestehen jetzt.
- Ein Installationsversuch traf auf die noch laufende Test-GUI und deren
  gesperrte Python-Dateien. Nach Beenden der Testinstanz gelang die Installation.

MSVC meldet weiterhin `LNK4098`-Warnungen über unterschiedliche C-Laufzeitbibliotheken
in einigen Zielen. Sie verhinderten weder Build noch die ausgeführten Tests.

## Direkte GUI-Prüfung

Die Prüfung verwendete ausschließlich ein separates Verzeichnis
`C:\Formorix Slicer\verification\gui-data`.

1. Der fertige Build startet; der Fenstertitel enthält **Formorix Slicer**.
2. **Formorix** erscheint in der vorhandenen Druckerauswahl, das Profil lädt mit
   seinem großen Bauraum. Alle drei Kopfgruppen sind im Einstellungsdialog vorhanden.
3. Die X-Positionen wurden im GUI auf **12,5**, **123** und **456 mm** geändert
   und als Benutzerprofil gespeichert. Die JSON-Datei enthält drei getrennte Werte.
4. Ein 20-mm-Testwürfel wurde importiert. Im 5D-Modus erscheint die erwartete
   Fehlermeldung; Slicing und ausführbarer Export bleiben gesperrt.
5. Das experimentelle Projekt wurde als `cube-20mm.3mf` gespeichert. Eine
   ZIP-Prüfung bestätigt: alle drei Kopfwerte vorhanden, **keine `.gcode`-Datei**.
6. Nach Wechsel auf das vorhandene **Prusa MK4**-Profil wurde derselbe Würfel
   erfolgreich geslict: 100 Schichten, 20 mm Höhe. Der lokale G-Code-Export
   umfasst 339829 Bytes und enthält `formorix_5d_enabled = 0`.
7. Dieser Vergleichsstand wurde separat als `cube-planar.3mf` gespeichert.
   Nach sauberem Beenden und Neustart wurde das experimentelle Projekt erneut
   geöffnet. Der GUI-Dialog zeigt wieder **12,5 / 123 / 456 mm** für Kopf 1 / 2 / 3.

Der WebView-Ersteinrichtungsassistent ließ sich vom verwendeten GUI-Werkzeug
nicht anklicken. Deshalb wurden die Testprofile vor dem GUI-Lauf im separaten
Testverzeichnis installiert. Die Verfügbarkeit in der Druckerauswahl wurde
anschließend gelesen; der komplette Erstinstallationsklickpfad wurde nicht geprüft.
Das ist eine Einschränkung der ausgeführten GUI-Automation.

Screenshots des geprüften Windows-Builds:

- [Bestehende Druckereinstellungen](images/formorix/existing-printer-settings.jpg)
- [Neue experimentelle Seite](images/formorix/experimental-settings.jpg)
- [Dritte Kopfgruppe](images/formorix/third-head-settings.jpg)
- [Gesperrtes 5D-Slicing](images/formorix/blocked-5d-slicing.jpg)
- [Herkömmliches Slicing](images/formorix/planar-slicing.jpg)

## Ergebnisdateien und Grenzen

Die lokal installierte Anwendung liegt unter
`C:\Formorix Slicer\Formorix-Slicer\build\OrcaSlicer\orca-slicer.exe`.
Interne Target- und Dateinamen bleiben aus Buildkompatibilitätsgründen erhalten.
Die installierte `OrcaSlicer.dll` ist mit der per GUI geprüften Build-DLL identisch.
Es wurde kein signiertes Installationspaket und kein Release veröffentlicht.

Vollständige lokale Protokolle liegen unter `C:\Formorix Slicer\`:
`deps-build.log`, `slicer-build-final.log`, `ctest-final.log`,
`clang-tidy-final.log`, `profiles-check.log` und `gettext.log`.
Der JUnit-Bericht liegt unter `verification\ctest-results-final.xml`;
GUI-Testprojekte und Wiederladebeleg liegen ebenfalls unter `verification`.
Builddateien, Testbenutzerprofile und ausführbare Testdruckdateien werden nicht
in das Quellcode-Repository aufgenommen.

Nicht geprüft wurden macOS/Linux-Builds, reale Drucker, Cloud-Anmeldung und
Hardware-Sendewege. Die neuen experimentellen Texte verwenden zunächst englische
Ausgangstexte mit den vorhandenen Übersetzungsmarkierungen.

Es gibt absichtlich noch keine 5D-Bahnplanung, Kinematik, Kollisionsprüfung,
gleichzeitige Kopfsteuerung oder Klipper-Ansteuerung. 75° bezeichnet derzeit die
einzelnen Achsengrenzen, keine berechnete räumliche Gesamtkippung. 0,4-mm-Düse und
Generic PLA sind vorläufige Profilplatzhalter. Alle Hardwarewerte benötigen vor
einer späteren Bewegungsimplementierung eine verbindliche Definition und Kalibrierung.
