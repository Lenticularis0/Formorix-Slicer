# Formorix Slicer – Auftrag 03

Stand: 10. November 2026, Windows x64. Entwicklungsbranch: `formorix/auftrag-03`.

## Ausgangsbasis und Integration

Die Änderungen und Testberichte von [PR #1](https://github.com/Lenticularis0/Formorix-Slicer/pull/1) und [PR #2](https://github.com/Lenticularis0/Formorix-Slicer/pull/2) wurden geprüft. Vor der Integration lief die bestehende Testsuite erneut: **3’084 bestanden, 6 übersprungen, 0 fehlgeschlagen**, 131,85 s. Die GUI-Auswahl wurde zusätzlich auf einer gewölbten Oberfläche und einer senkrechten Wand ausprobiert und als 3MF gespeichert.

Die PRs wurden in dieser Reihenfolge nach `main` integriert:

1. PR #1: `839417936fd742ec99b835c6235795faec552f1d`.
2. PR #2: `2becc77669bc0fbebadc9b87c3604c1a1cc2ee8c`.

Auf dem zweiten Merge basiert der neue Branch. Orca-Upstream, Git-Historie, Lizenz, vorhandene Profile und die Sperren für experimentelle Druckaufträge bleiben erhalten. Das separate CAD-Repository wurde nicht verändert.

## Implementierung

- Der aktive markierte Bereich wird mit seinen tatsächlichen Pinsel-Unterteilungen und der gewählten Modellinstanz übernommen. Zusammengefasste Regionen bilden ein gemeinsames Netz.
- Ein Raster in einer gemeinsamen Tangentialprojektion wird auf die Oberfläche zurückgeführt. Die Zeilen wechseln ihre Richtung und werden am Rand verbunden. Dreiecks- und ehemalige Regionsgrenzen verursachen keine Extrusionsunterbrechung. Zusätzliche Punkte an Dreieckskanten verhindern Abkürzungen durch konkave Oberflächen.
- Mehrere Lagen entstehen entlang interpolierter, flächengewichteter Normalen nach innen. Anzahl, Schalendicke, feinere letzte Lagen, Richtungswechsel, Randabstand und benutzerdefinierte beziehungsweise referenzierte Richtung werden berücksichtigt. Die äusserste TCP-Lage liegt auf dem ursprünglichen Netz.
- Wanddicke wird durch Strahlen ins geschlossene Ausgangsmodell geprüft. Getrennte Bereiche, Löcher, ungeeignete Projektionen, umgeklappte Offsets, fehlende Wandstärke und Ressourcenüberschreitungen werden abgelehnt. Fehlgeschlagene Bereiche erscheinen rot; es bleiben keine Teilbahnen zurück.
- Die gewünschte Düsenachse folgt der lokalen Normale. Ausdrücklich erlaubte Abweichung bis 15° und die Freigabe senkrechter Flächen werden mit der konfigurierten maximalen Neigung abgeglichen. Es entstehen TCP-Posen, keine Maschinenachsenbefehle.
- Die vorhandene Vorschau und der Plater-Canvas zeigen Bahnen, Lagen, angenäherte Schale/Kern, Pfeile, Start/Ende und drei kleine nummerierte Düsenspitzen. Die Zeitleiste bietet Start/Pause, Anfang, ±1 s und vier Wiedergabegeschwindigkeiten. Bereits durchlaufene Bahnen werden grün, zukünftige golden dargestellt.
- Drei unabhängige, absolut zeitbasierte Kopfspuren sind bereits vorgesehen. Der Generator füllt genau eine Spur. Die Interpolation unterstützt gleichzeitig belegte Spuren; ein Scheduler wird nicht implementiert.
- Hilfe-Schaltflächen verwenden eine zentrale `FormorixHelpTopic`-Zuordnung. Zehn versionierte Themenseiten ersetzen ausführliche Parametertexte. Die Ziele können zentral auf ein Wiki umgestellt werden.

Details: [Architektur](HLSD/FormorixPreview.md), [Benutzerhilfe](formorix/index.md), [Demonstration](formorix/demo.md).

## Build und automatisierte Tests

Windows Release mit Visual Studio 2026 / MSVC 14.51, Windows SDK 10.0.26100 und CMake 4.4.3. Die bestehenden Abhängigkeiten wurden weiterverwendet; keine neue Laufzeitbibliothek wurde eingeführt.

| Prüfung | Tatsächliches Ergebnis |
| --- | --- |
| Release `ALL_BUILD`, Anwendung und Tests | Erfolgreich. |
| Getrennte Installation `build/Formorix-Abnahme03` | Erneut gebaut, installiert und live geprüft. |
| Neue `[FormorixPreview]`-Tests | 7 Testfälle, **58’064 Assertions bestanden**. |
| Gesamte CTest-Auswahl | **3’091 bestanden, 6 übersprungen, 0 fehlgeschlagen**; 3’097 ausgewählt, beim abschliessenden Wiederholungslauf 245,37 s. |
| `run_clang_tidy.ps1` gegen `origin/main` | Erfolgreich für die geänderten C++-Dateien. |
| Vollständiger gettext-Lauf | Erfolgreich; neue deutsche GUI-Texte kompiliert. |
| Bestehende Formorix-Speicher-/Exporttests | In der gesamten Testsuite bestanden. |

Die CTest-Auswahl verwendet wie zuvor `-LE 'NotWorking|RequiresApp'`. Sechs Tests benötigen unter Windows nicht verfügbare Symlink-Rechte und werden übersprungen. Die Linux-spezifische CLI-Suite ist unter Windows nicht registriert. NumPy 2.5.3 wurde für die vorhandenen Python-Bridge-Tests im separaten Testinterpreter installiert.

Ein erster Build mit hoher Parallelität erschöpfte den Speicher beim Kompilieren der Test-PCHs. Der erneute vollständige Build mit `--parallel 2 -- /p:CL_MPCount=2` war erfolgreich. MSVC meldet weiterhin Warnungen zu CRT-Bibliotheken (`LNK4098`) und einem unsigned-char/bool-Vergleich im unveränderten Upstreamcode. Die abschliessenden GUI-Nachbesserungen wurden erneut als Windows Release gebaut, installiert und live geprüft.

Die neuen Tests in [test_formorix_preview.cpp](../tests/libslic3r/test_formorix_preview.cpp) prüfen:

| Geometrie / Verhalten | Abdeckung |
| --- | --- |
| Geneigte Ebene | Normaler Lagenabstand, Kontur, Orientierung, Kontinuität. |
| Glatte konvexe und konkave Wölbung | Mehrere echte nichtplanare Lagen; äussere Punkte und Segmentmitten auf dem Ausgangsnetz. |
| Zwei angrenzende zusammengefasste Flächen | Tatsächliche Masken-Zusammenfassung und Extrusion über die frühere Grenze. |
| Senkrechte Wand | Ablehnung ohne Freigabe; 75° Sollneigung bei erlaubten 15° Abweichung. |
| Enger Radius | Ungültiger innerer Offset wird ohne verbleibende Teilbahn abgelehnt. |
| Einfaches Hydrofoil | NACA-0012-Oberseitenabschnitt auf einem geschlossenen Testkörper. |
| Ganze Wand / Schalendicke | Gleichmässige Wand, variable Wand-Ablehnung, unzureichende Wandstärke. |
| Zeitmodell | Position und sphärische Achsinterpolation, Bewegung/Warten, Lücken, drei überlappende Spuren, ungültige Daten. |
| Weitere Grenzen | Offener Körper, Loch, getrennte Bereiche, Abbruch, Ressourcenlimit, Referenzrichtung. |

Die vorhandenen Print-Tests prüfen weiterhin die Gleichheit konventioneller Bewegungsbefehle sowie die Ablehnung experimenteller Exporte. Kompakte Prüfnachweise liegen unter [verification](formorix/verification/summary.json).

## GUI-Prüfung

Die Live-Prüfung erfolgt mit dem synthetischen Projekt [curved-wall.3mf](formorix/examples/curved-wall.3mf), dem Profil **Formorix 5D** und **Generic PLA**. Sie verändert keine geöffneten Benutzerprojekte.

Geprüft: tatsächliche Flächenmarkierung, Speichern und Neustart, Wiederladen von Name **Wölbung** und vier Lagen, Berechnung der Wölbung, Start/Pause und Zeitinterpolation, Lagenfilter, Kernanzeige sowie die senkrechte Wand mit und ohne zulässige Abweichung. Für die gespeicherte Wölbung ergibt die aktuelle Profilgeschwindigkeit **213,8 s**, für die separat ausgewählte Wand mit fünf Lagen **124,5 s**. Diese Werte sind Vorschau-Schätzungen.

Screenshots und Bedienfolge stehen in der [Demonstration](formorix/demo.md). Die gespeicherte 3MF enthält keine ausführbaren G-Code-Einträge. Die erzeugten Vorschau-Bahnen werden nicht exportiert und nach dem Öffnen erneut berechnet.

**Abschliessender GUI-Nachtest:** Die separate Testinstallation wurde mit dem gespeicherten Wölbungsprojekt gestartet. Folgende Prüfungen wurden tatsächlich durch die GUI ausgeführt:

| Bedienung | Beobachtung |
| --- | --- |
| Zeitschieber | Sprung von 0 auf 109,0 s; Düse 1 und Fortschrittsfarbe bewegen sich. Am Ende bei 213,8 s steht Kopf 1 auf „Wartet“. |
| +1 s / −1 s / Anfang | 109,0 → 110,0 → 109,0 → 0,0 s. |
| Geschwindigkeit / Start / Pause | 16× gewählt, gestartet und bei 86,8 s pausiert. Die nächste Aufnahme bestätigt unveränderte 86,8 s. |
| Beschriftungen und Werkzeuge | Alle drei Düsen tragen lesbare Nummern; die Bearbeitungsgizmos sind während der Bahnvorschau ausgeblendet. |
| Hilfe | Vorschau- und Schichtdicken-Schaltflächen öffnen die entsprechenden GitHub-Seiten im Browser; die Fenstertitel bestätigen `preview.md` und `layers.md` am festgelegten Commit. |
| Normale Orca-Vorschau | Umschalten funktioniert; das experimentelle Formorix-Profil bleibt für Slicing/Export gesperrt. |
| Konventionelles Slicing danach | `cube-planar.3mf`, Prusa MK4, Generic PLA, 0.20mm Standard @MK4: erfolgreich gesliced, 100 Lagen bis 20 mm, normale Bahnen und G-Code-Vorschau vorhanden, Export-Schaltfläche aktiv. |

Die Zeitleiste zeigt dauerhaft **„Nur 5D-Teil: ohne normalen Bauteilaufbau, Supports und Wartezeiten.“** und **„Verfahrbewegungen beim Lagenwechsel: Kollisionsprüfung ausstehend.“** Der Zustand einer Verfahrbewegung enthält ebenfalls den ungeprüften Kollisionsstatus. Diese Verbindungen sind keine freigegebenen Maschinenbewegungen. Die Uhr ist zusätzlich mit **5D** gekennzeichnet.

Die zentrale Hilfezuordnung verweist auf den veröffentlichten Commit `10177f248099015508246693bb67d8a5b0357101`, der beim Merge Teil der Historie bleibt. Das Löschen des Entwicklungsbranches bricht diese Links nicht. Alle zwölf Ziele wurden direkt von GitHub abgerufen (HTTP 200); sämtliche verwendeten expliziten HTML-Anker wurden im abgerufenen Inhalt geprüft. Die separate Freigabe für eine vertiefte Browser-Fensterprüfung lief ab; der tatsächliche GUI-Aufruf, die geladenen Seitentitel sowie Inhalt und Anker wurden auf den beschriebenen Wegen geprüft. [Linkprüfprotokoll](formorix/verification/help-links.json).

Die neuen Screenshots in der Demonstration stammen aus diesem abschliessend geprüften Build. Geöffnete Hydrofoil- und FreeCAD-Benutzerprojekte wurden nicht verändert.

## Bekannte Grenzen

- Unterstützt wird jeweils ein zusammenhängender, lochfreier, glatter Bereich, der in einer Tangentialprojektion als Höhenfeld und mit einem Intervall pro Rasterzeile darstellbar ist. Vollständig umlaufende Flügelprofile, Überhänge innerhalb desselben Patches und komplexe verzweigte Flächen sind noch nicht allgemein lösbar.
- Vollwandfertigung ist nur bei nahezu gleichmässiger normaler Wanddicke vorgesehen. Variable Dicke wird ausdrücklich abgelehnt. Die Vorschau arbeitet auch bei deaktivierter Option „Originalabmessungen beibehalten“ nach innen.
- Selbstüberschneidungsprüfungen sind begrenzte geometrische Prüfungen, kein vollständiger globaler Nachweis. Schale und Kern sind Anschauungsgeometrie; kein druckfertiger Volumenkörper oder simuliertes Extrusionsvolumen.
- Die Randverbindungen können sich zwischen benachbarten Rasterzeilen wiederholen. Eine Optimierung von Materialanhäufungen, Extrusionsmenge, Beschleunigungen und optimalem Bahnabstand fehlt.
- Lagenwechsel sind ungeprüfte gerade Verbindungen. Die Gesamtzeit umfasst ausschliesslich den berechneten 5D-Vorgang; normaler Kern, Unterbau, Supports und Maschinenwartezeiten sind nicht eingeplant.
- Die beiden Köpfe ohne Aufgabe haben schematische Markierungspositionen, keine kalibrierten Parkpositionen. Alle drei Spuren können dargestellt werden, aber nur eine wird tatsächlich geplant.
- Keine Mehrkopfaufteilung, Kollisionsprüfung, automatische 5D-Supports, verwobenen Mehrkopf-Bahnen, inverse Maschinenkinematik, Klipper-Ansteuerung oder ausführbaren 5D-Dateien.
- Laufzeit und Darstellung wurden unter Windows geprüft. macOS/Linux, weitere DPI-Skalierungen und reale Hardware sind nicht geprüft. Es wurde kein signierter Installer veröffentlicht.

## Vorschläge für Auftrag 04

1. Referenzmodell und Messkonventionen für TCP, Düsenradius und Extrusionsquerschnitt festlegen; Genauigkeit gegen bekannte CAD-Geometrien messen.
2. Robuste Patch-Zerlegung für grössere Wölbungen, Löcher und variable Wandstärken entwickeln; globale Offset- und Abstandsprüfungen ergänzen.
3. Sichere Lagenverbindungen und eine validierte Einzelkopf-Kinematik mit Düsen-/Bauteilabstand untersuchen, weiterhin ohne ausführbaren Export.
4. Zeitmodell um Beschleunigung und definierte planare Unterbauphasen erweitern. Mehrkopfplanung erst nach Festlegung der Mechanik und Sicherheitsbedingungen beginnen.

Diese Vorschläge sind nicht implementiert.
