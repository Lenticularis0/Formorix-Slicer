# Auftrag 04 – Hybrid-Slicing-Prototyp

Historischer Ausgangsbericht. Die späteren Korrekturen an Materialmenge, Übergängen und Auflagenprüfung sowie die aktuelle Abnahme stehen im [Nachtrag zu Auftrag 04](Formorix-Auftrag-04-Nachtrag.md). Die folgenden Messwerte dokumentieren den ursprünglichen Stand.

Der Prototyp verbindet gespeicherte Formorix-Oberflächenmarkierungen mit einer CGAL-Kern-/Schalenteilung, echten konventionellen Orca-Extrusionsbahnen und einer gemeinsamen 3D-Vorschau. Die experimentelle Exportsperre bleibt aktiv. Es entstehen keine ausführbaren Hybrid-/5D-Druckdateien.

**Die geometrische Gesamtabweichung von höchstens 0,01 mm ist nicht nachgewiesen.** Die berechneten Volumenkörper ergänzen sich, aber die nominale Materialablage hat weiterhin einen erheblichen Volumenüberschuss und Überstände an Regionsrändern. Geringe Oberflächen-Stichprobenfehler sind keine globale Genauigkeitsbestätigung und keine Aussage über reale Druckgenauigkeit.

## Ausgangsstand und Reihenfolge

PR #4 wurde am 11. November 2026 vor Beginn der Integration geprüft. Die Windows-GUI-Abnahme umfasste Auswahl auf gewölbten und senkrechten Flächen, Zusammenfassen, Projekt-Rundlauf, gemeinsame Supportwerte, Kopf-/Materialzuordnung, Zeitschieber, ±1 s, Wiedergabe, Wiki-Aufruf und normales Orca-Slicing. [Schritte und Screenshots](formorix/verification/auftrag-04/pr4-gui-regression.md).

Die damalige vollständige verfügbare Regression bestand mit **3.110 erfolgreichen Tests, sechs Windows-Symlink-Skips und null Fehlern**. PR #4 wurde mit geprüftem Head `33cbcb2de841f7c4d3b7373a6cb378872a88d440` in `main` zusammengeführt; Merge-Commit `e02580397ecb31b0785cf709717e503935ce97d9`. Anschließend entstand `formorix/auftrag-04`.

## Implementierung

- `FormorixHybrid` verarbeitet alle markierten Regionen einer gewählten Modellinstanz. Das Originalmesh bleibt unverändert. Double-Precision-Geometrie und CGAL berechnen beschnittene, gültige Schalen, Restkern, gemeinsame Grenzflächen und Volumenprüfungen.
- Schichtanzahl, Schalendicke, feinere Schlusslagen und gleichmäßig dicke Vollwände gehen in die Lagenaufteilung ein. Ungültige Offsets, Überlagerungen, unzureichende Wandstärke und nicht unterstützte Randabschlüsse werden abgewiesen.
- `FormorixCorePreview` lässt den tatsächlichen Restkern durch Orcas `Print::process()` laufen und übernimmt native Wände, Infill, Solid Infill, Top-/Bottom- und Support-Extrusionen. Es gibt keine zweite konventionelle Slicing-Engine und keinen temporären G-Code-Export.
- Der unveränderliche `Print::Purpose::HybridPreview` sperrt sämtliche Exportvarianten einschließlich direktem GCode-Aufruf, vorheriger Datei und Cache. Eine Konfigurationsänderung oder `clear()` hebt diese Sperre nicht auf.
- Die 5D-Bahnen kompensieren den nominalen elliptischen Materialquerschnitt und unterscheiden Materialzentrum, Kontaktpunkt und TCP. Adaptive Verfeinerung prüft Mittelpunkte und Orientierungsänderungen. Querabstand und Längsabtastung sind getrennt; begrenzte Gesamtbudgets schützen große Modelle vor unkontrolliertem Speicherverbrauch.
- Abdeckung, Materialvolumenbilanz, lokale Kernüberlappung und Kontakt zu tatsächlichen Orca-Bahnen werden ausgewertet. Fehlende Hüllenproben ergeben unendliche Fehlerwerte. Es erfolgen keine unbelegten Flow-Korrekturen.
- Drei unabhängige Köpfe binden eigene Orca-Material- und Düsenprofile sowie explizite Düsenslots. Inhaltsbasierte Signaturen erkennen auch ungespeicherte Änderungen unter demselben Profilnamen. Gespeicherte Prozess-Snapshots werden vor Verwendung verworfen beziehungsweise neu aufgelöst. [Profilprüfbericht](formorix/verification/auftrag-04/profiles.md).
- Die Vorschau trennt Original, Kern, native Kern-/Supportbahnen, Schale, 5D-Bahnen, Grenzflächen, fehlende Auflage und ungeprüfte Bereiche. Die drei Düsenmarkierungen und die 5D-Zeitleiste bleiben erhalten. Kernbahnen sind ausdrücklich nicht zeitlich synchronisiert.

Architektur: [FormorixHybrid](HLSD/FormorixHybrid.md), [Profilauflösung](HLSD/FormorixProfiles.md). Bedienung: [Hybrid-Vorschau](formorix/hybrid.md).

## Numerische Ergebnisse und Grenzen

Der [geometrische Prüfbericht](formorix/verification/auftrag-04/geometry.md) enthält sämtliche Testmodelle, Maximal-/Mittel-/RMS-Werte, Volumenresiduen, Konvertierungsfehler, Lücken, Überstände und Materialvolumenfaktoren. Die [Rohwerte](formorix/verification/auftrag-04/geometry-tests.txt) bleiben mitversioniert.

Beim real gespeicherten Wölbungsprojekt mit dem Standardprofil 0,4-mm-Düse, 0,42-mm-Linienbreite, 0,2-mm-Schichtdicke und Flow 0,98 betragen die nominalen Oberflächen-Stichprobenfehler **0,001389 mm maximal, 0,000497 mm im Mittel und 0,000692 mm RMS**. Das Volumenresiduum der Kern-/Schalenaufteilung beträgt **3,64 × 10⁻¹² mm³**. Gleichzeitig liegen der nominale Materialvolumenfaktor bei **4,642359** und der Projektionsüberstand bei **1,729853 %**. Deshalb wird dieser Fall als berechenbare Vorschau mit ungültiger Materialbilanz dargestellt.

Die senkrechte Wand mit 15° Düsenabweichung erreicht im Test **0,019992 mm** maximalen nominalen Stichprobenfehler und erzeugt **0,098656 mm** lokale innere Hüllenüberlappung. Diese Abweichungen werden erkannt und nicht als Zielerfüllung gewertet.

Unterstützt sind lokal eindeutig projizierbare zusammenhängende Bereiche: ebene/geneigte Flächen, konvexe/konkave Wölbungen, begrenzte Hydrofoil-Negativflächen, benachbarte Regionen mit verschiedenen Tiefen und Materialien sowie gleichmäßig dicke Vollwände. Vollständig markierte geneigte, konkave und Hydrofoil-Oberseiten können an ihren Seitenrändern eine noch fehlende lokale Bahnterminierung benötigen. Ein vollständig umlaufendes Flügelprofil ist weiterhin kein allgemein unterstützter Planungsfall.

Weitere ausdrückliche Grenzen:

- Unbekannte CAD-Referenz, gekrümmte Flächenabdeckung und adaptive Interpolation besitzen keine vollständigen Fehlerschranken. Float-/Orca-Rasterfehler werden gesondert gemessen; sämtliche internen Orca-Offset-/Ablagefehler sind damit nicht bewiesen.
- Dichte Übergangszonen und zusätzliche 5D-Stützen werden nicht automatisch erzeugt. Kontakt an Stichproben beweist weder Tragfähigkeit noch Haftung oder eine gültige Bauabfolge.
- Nichtneutrale XY-/Z-Schrumpfkompensation, Objekt-/Teilüberschreibungen, negative Volumen, Modifikatoren, konventionelle Facettenbemalung und variable Orca-Schichthöhen werden im Hybridpfad ausdrücklich abgewiesen, statt stillschweigend verloren zu gehen.
- Ein separater Supportkopf nutzt einen eigenen Orca-Pass. Dies ist keine gemeinsame Mehrkopf-Zeitplanung. Regionale 5D-Bahnen erscheinen nacheinander; die Vorschau behauptet keinen parallelen Fertigungsablauf.
- Keine Maschinenkinematik, Kollisionsfreigabe, Klipper-Anbindung oder reale Druckprüfung. Ungeprüfte Lagenwechsel bleiben entsprechend gekennzeichnet.

## Reproduzierbare Befehle

```powershell
cmake --build build --config Release --target ALL_BUILD --parallel 2 -- /p:CL_MPCount=8 /nodeReuse:false
ctest --test-dir build/tests -C Release -LE 'NotWorking|RequiresApp' --no-tests=error --output-on-failure --timeout 180 -j 2
scripts/run_clang_tidy.ps1 -Base origin/main -NoFetch -Jobs 2 -Yes
scripts/run_gettext.bat --full
```

Die Testgruppen `NotWorking` und `RequiresApp` werden entsprechend ihrer Repository-Kennzeichnung nicht in die automatisierte Vollsuite einbezogen. Die native GUI wird gesondert bedient. Andere Betriebssysteme und physische Druckhardware sind nicht geprüft.

## Abschließende Build- und Testergebnisse

Geprüfter C++-Quellstand: **`8a2159e9f56aa2663dee513b0f303119d4a23d8b`**. Windows x64 Release, MSVC 19.51.36256 / Visual Studio 18; `ALL_BUILD` und CMake-Installation erfolgreich. Der direkt startbare lokale Build liegt unter `build/Formorix-Auftrag04/orca-slicer.exe`. [SHA256-Prüfsummen und Buildzuordnung](formorix/verification/auftrag-04/build-manifest.txt), [Buildprotokoll](formorix/verification/auftrag-04/build.txt).

| Prüfung | Tatsächliches Ergebnis |
| --- | --- |
| Vollständige verfügbare CTest-Regression | **3.138 bestanden, 6 Symlink-Skips, 0 Fehler**; 3.144 ausgewählt, 259,65 s. [Protokoll](formorix/verification/auftrag-04/regression.txt). |
| Hybrid-Geometrie einschließlich Vollwand und Ressourcenbudget | 13 Fälle / 2.194 Assertions bestanden. |
| Native Kernintegration und Exportschutz | 13 Fälle / 5.203 Assertions bestanden. [Protokoll](formorix/verification/auftrag-04/core-tests.txt). |
| Bisherige 5D-Vorschau | 7 Fälle / 58.064 Assertions bestanden. |
| Profile und Oberflächen-/3MF-Rundlauf | 13 Fälle / 168 Assertions bestanden; native Materialvarianten zusätzlich 1 Fall / 352 Assertions. |
| clang-tidy | Gesamtcheck aller 25 betroffenen C++-Dateien erfolgreich; abschließender Profilfix in 3 Dateien erneut erfolgreich geprüft. [Gesamtcheck](formorix/verification/auftrag-04/clang-tidy.txt), [Nachprüfung](formorix/verification/auftrag-04/clang-tidy-profile-fix.txt). |
| Deutsche neue GUI-Texte | 38 Einträge ergänzt; vollständiger gettext-Lauf erfolgreich. Katalogprüfung: 0 fehlerhafte Einträge, 63 bereits bestehende Warnungen. |
| Windows-GUI | Gemeinsame Vorschau, getrennte Anzeigen, Zeitsteuerung, Bericht, Kopfbindungen, 3MF-Rundlauf und normales Orca-Slicing tatsächlich bedient. [Prüfbericht und Bilder](formorix/verification/auftrag-04/gui.md). |

Die Linker melden weiterhin die bereits vorhandenen `LNK4098`-/LTCG-Hinweise. Zwischenläufe scheiterten an Dateisperren durch gleichzeitig verwendete Test-/Python-Dateien; nach serieller Build-/Testausführung bestand der abschließende vollständige Build. Die eingeschränkte Umgebung blockierte außerdem Windows-gettext und Loopback-Tests; die dokumentierten erfolgreichen Läufe erfolgten in der normalen lokalen Umgebung. Es wurden keine Betriebssystem-Schutzfunktionen verändert.

Der isolierte Kern-Testlauf protokolliert außerdem einen Lesefehler für `info/nozzle_info.json` in seiner Testressourcenumgebung. Die 5.203 Assertions dieses Laufs bestanden; eine Aussage über die optionale Düsenhärte-Datenbank wird daraus nicht abgeleitet.

Die GUI-Abnahme fand und behob drei zusätzliche Probleme: unnötig dichte Längsabtastung beim echten 0,4-mm-Profil, abgeschnittene Zeitleistenhinweise und eine reine G-Code-Makrovoraussetzung im internen Kernpfad. Die Makroprüfung bleibt für normale ausführbare Druckaufträge aktiv. Fortschrittsanzeigen werden auch beim Abbruch einer Hybridberechnung beendet.

Die zentralen Hilfelinks zeigen auf den veröffentlichten Commit `967bbae1da8a5d5cb8a657a37b50aab2ac13ec6b`. Elf Zieldateien einschließlich `hybrid.md` und die verwendeten Anker wurden über GitHub überprüft; der GUI-Hilfeaufruf öffnete die richtige Vorschauseite. Der Entwicklungsbranch kann daher später gelöscht werden, ohne diese Links zu beschädigen.

Auftrag 05 wurde nicht begonnen. Es wurden keine GitHub-Issues erstellt.
