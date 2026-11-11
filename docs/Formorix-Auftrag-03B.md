# Formorix Slicer – Auftrag 03B

Stand: 11. November 2026. Ausgangspunkt ist der zusammengeführte Auftrag 03, Commit `7fb330b60a2450473be86b17067257b62216d669`. Die neuen Module sind unabhängig testbare Grundlagen. Sie ersetzen noch nicht den Vorschau-Algorithmus aus Auftrag 03 und sind nicht an Orcas planare Bahnplanung angeschlossen.

## Ergebnis und Genauigkeitsaussage

Es gibt jetzt eine validierte Schalen-/Kernaufteilung, ein mathematisch definiertes nominales Ablagemodell, adaptive Genauigkeitsmessungen, getrennte Prozessprofile für drei Köpfe, Fertigungsabhängigkeiten und parametrisierbare Maschinenhüllen. Die Geometrie bleibt in Double-Precision; CGAL verwendet für die Boolean-Konstruktionen exakte Zahlen.

**0,01 mm wird für die dokumentierten analytischen Einzelprüfungen erreicht, nicht pauschal für beliebige importierte Bauteile oder den realen Druck.** Ohne CAD-Referenz bleibt der Importfehler unbekannt. Ein grobes Netz und eine zu breite Linie auf einem engen Radius werden ausdrücklich als unzureichend erkannt. Die bisherigen GUI-Vorschaubahnen erhalten durch diese Module kein nachträgliches Genauigkeitszertifikat.

Die [Architekturbeschreibung](HLSD/FormorixFoundations.md) enthält Schnittstellen, Formeln und ihre Voraussetzungen. Die [maschinenlesbaren Messwerte](formorix/verification/auftrag-03b.json) ergänzen die folgenden Tabellen.

## Geometrie und Materialablage

`FormorixGeometry` bildet aus einer orientierten Fläche, lokalen Normalen und einer Tiefe je Vertex einen geschlossenen Schalenkörper. CGAL berechnet Schnittmenge und Restkörper, prüft geschlossene orientierte Volumen, Selbstüberschneidungen, Materialüberlappung und Volumenerhaltung. Unterschiedliche Bereichstiefen, variable Wandstärke, durchgehende umlaufende Flächen und ein Bereich mit Loch sind getestet. Gemeinsame Grenzflächen besitzen eigene Bereichs-IDs und gemeinsame Dreieckskoordinaten.

Nicht verfügbare Wandstärke und überlappende Bereichszuordnungen werden abgelehnt. Es gibt kein stilles Abschneiden oder Vorrangrecht für das zuerst zugeordnete Material. Die Ausgangsfläche muss tatsächlich auf dem Ausgangskörper liegen. Ein geometrisch leicht versetzter Patch wird nicht automatisch zurückprojiziert.

Beim Umwandeln der exakten Boolean-Ergebnisse in Double-Koordinaten werden alle Teile auf dasselbe Raster mit Schrittweite 2^-30 mm gelegt. Dadurch kollabierende Nullflächen werden topologisch bereinigt; neue Koordinaten oder eine Glättung sind nicht erlaubt. Ungültige Körper und positive Materialüberlappung nach der Konvertierung führen zum Abbruch. Der tatsächliche Rundungsfehler und die Volumenrestabweichung werden ausgewiesen. Das Raster ist wesentlich feiner als 0,01 mm und ersetzt keine CAD-Genauigkeitsprüfung.

Orcas bestehendes `MeshBoolean::cgal` rundet seine Ausgabe auf Float zurück. Für die neuen Präzisionsmodule wird deshalb derselbe vorhandene CGAL-Build verwendet, aber mit einer eigenen Double-Ausgabe. Es wurde keine zusätzliche Geometriebibliothek eingeführt. LSCM-Parametrisierung ist für geeignete scheibenförmige Patches nützlich; sie löst weder allgemeine Topologie noch fehlende CAD-Genauigkeit. Als robuste Grundlage dient ein verzerrungsfreier Atlas pro Dreieck mit Übergängen über gemeinsame Kanten. Rechnerische Teilflächen sind keine Drucknähte.

`FormorixProcess` beschreibt einen elliptischen Querschnitt senkrecht zur Bahntangente. Tatsächliche Linienbreite, Schichtdicke, Quer-Neigung und Korrekturfaktoren bestimmen die Form. Der Stützpunkt dieser Ellipse wird auf den CAD-Kontaktpunkt gelegt; Linienmittelpunkt und TCP sind davon getrennt. Eine vorgegebene Kontaktkurve kann adaptiv kompensiert und diskretisiert werden. Damit ist noch keine allgemeine Flächenfüllung implementiert.

Der nominale Bahnabstand berücksichtigt die Rautiefe zwischen benachbarten Ellipsen. Eine zusätzliche Bilanz vergleicht das zugeführte Volumen mit der Vereinigungsfläche einer periodischen Reihe gleichartiger, horizontaler Querschnitte. Mehrfach überdecktes Material und ein theoretischer Bilanzfaktor werden ausdrücklich ausgewiesen. Dieser Faktor wird nicht automatisch auf den Flow angewendet: Die reale Linienform würde sich dabei möglicherweise ändern. Eine volumen- und massenerhaltende Schmelzströmung wird noch nicht simuliert. Gemischte Linienbreiten, Krümmung und Neigung brauchen eine weitergehende Bilanz. Schrumpfung, Benetzung, Längsneigung, Kühlverlauf und reale Formänderung brauchen Materialmessungen. Eine geometrisch passende Grenze ist kein Haftungsnachweis.

## Numerische Prüfungen

Alle Längenabweichungen sind in **mm** angegeben. Maximalwert, Mittelwert und RMS stammen aus den tatsächlich ausgeführten Abstandsprüfungen. Mittelwert/RMS sind Quadraturwerte; die zusätzliche Obergrenze deckt bei den analytischen Referenzen die Zwischenräume ab. Netzprüfungen messen gerichteten Abstand Netz → Referenz, keinen symmetrischen Hausdorff-Abstand und keine fehlende Flächenabdeckung. Für Kurven wird eine analytische Schranke der zweiten Ableitung verwendet. Ressourcenlimits führen zu „nicht ausreichend verifiziert“.

Interne Kopf-IDs 0/1/2 entsprechen den Köpfen 1/2/3 der Benutzeroberfläche.

| Messung | Maximum | Mittelwert | RMS | Obergrenze | Ziel 0,01 mm |
| --- | ---: | ---: | ---: | ---: | --- |
| circumferential_import | 0,00361363 | 0,00242134 | 0,00263895 | 0,00384243 | erfüllt |
| circumferential_inner_interface | 0,00331250 | 0,00221956 | 0,00241904 | 0,00352223 | erfüllt |
| sphere_two_curvatures | 0,00021117 | 0,00014302 | 0,00014493 | 0,00042431 | erfüllt |
| convex_fine | 0,00023437 | 0,00016927 | 0,00017372 | 0,00047172 | erfüllt |
| convex_coarse | 0,23681702 | 0,15658515 | 0,17192137 | 0,23706142 | abgelehnt |
| concave_fine | 0,00023437 | 0,00016927 | 0,00017372 | 0,00047172 | erfüllt |
| concave_coarse | 0,23681702 | 0,15658515 | 0,17192137 | 0,23706142 | abgelehnt |
| convex_compensated_contact | 0,00019336 | 0,00012246 | 0,00013781 | 0,00019336 | erfüllt |
| concave_compensated_contact | 0,00019726 | 0,00012493 | 0,00014059 | 0,00019727 | erfüllt |
| NACA0012_CAD_polyline | 0,00038254 | 0,00007095 | 0,00010207 | 0,00099124 | erfüllt |
| planar_bead_head_0 | 0,00500000 | 0,00166158 | 0,00223247 | 0,00500000 | erfüllt |
| planar_bead_head_1 | 0,00500000 | 0,00165299 | 0,00222419 | 0,00500000 | erfüllt |
| planar_bead_head_2 | 0,00500000 | 0,00165874 | 0,00222974 | 0,00500000 | erfüllt |
| concave_bead_0 | 0,00659010 | 0,00219089 | 0,00294244 | 0,00670696 | erfüllt |
| convex_bead_0 | 0,00340460 | 0,00112788 | 0,00151632 | 0,00670696 | erfüllt |
| concave_bead_1 | 0,00578509 | 0,00191346 | 0,00257356 | 0,00581362 | erfüllt |
| convex_bead_1 | 0,00421229 | 0,00138864 | 0,00186947 | 0,00581362 | erfüllt |
| concave_bead_2 | 0,00618762 | 0,00205363 | 0,00275942 | 0,00625246 | erfüllt |
| convex_bead_2 | 0,00380842 | 0,00125971 | 0,00169429 | 0,00625246 | erfüllt |
| planar_interface | 0,00000000 | 0,00000000 | 0,00000000 | 0,00000000 | erfüllt |
| tight_radius_import | 0,00001628 | 0,00001628 | 0,00001628 | 0,00007560 | erfüllt |

Die zu breite Linie auf Radius 0,15 mm erreicht zusätzlich eine beobachtete Abweichung von **0,07561028 mm** und verfehlt das Ziel. Die feine Eingangsgeometrie allein genügt hier nicht.

Volumenkontrolle: Die umlaufende Schale hat gegenüber dem analytischen Polygonring rund **−3,71e-9 mm³** Volumenabweichung. Für Schale plus Kern beträgt der Residualwert **−1,5683e-9 mm³** bei **84,68680924 mm³** Eingangsvolumen. Die grösste gemessene Vertex-Rundungsschranke beträgt **5,6864e-10 mm**. Benachbarte Tiefen und variable Wand liefern im Testlauf einen Volumenresidualwert von 0 mm³. Das ist eine numerische Volumenaussage, keine reale Fertigungsmessung.

Die nominale Überdeckungsbilanz zeigt, warum die Aussenhüllfläche allein nicht genügt. Bei 0,005 mm gewünschter Rautiefe ergeben sich:

| Kopf / Düse | Bahnabstand mm | Zugeführte Querschnittsfläche mm² | Vereinigungsfläche je Zelle mm² | Reiner Bilanzfaktor | Abweichung zur numerischen Integration mm² |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 / 0,8 mm | 0,19554028 | 0,27646015 | 0,07756760 | 0,28057426 | 6,602e-12 |
| 2 / 0,4 mm | 0,13738996 | 0,06911504 | 0,02702470 | 0,39101049 | 4,701e-12 |
| 3 / 0,6 mm | 0,16898521 | 0,15550884 | 0,05013609 | 0,32240025 | 5,731e-12 |

Die Flächen entsprechen dem Volumen je Millimeter Bahnlänge. **Die Bilanzfaktoren sind keine Flow-Empfehlungen.** Das ungeänderte Ellipsenmodell würde bei diesen engen Abständen deutlich zu viel Material zuführen. Die Formänderung bei reduziertem Flow muss erst kalibriert werden; das System wendet keine dieser Zahlen auf reale Extrusion an.

| Testgruppe | Geprüfte Eigenschaft / Einheit |
| --- | --- |
| Ebene Schale und Kern | Volumen 100 + 400 = 500 mm³; gemeinsame Grenzfläche bei Z = 4; vollständiger Wandverbrauch ergibt einen Körper mit 500 mm³. |
| Benachbarte unterschiedliche Tiefen | Volumen 50 + 100 + 350 = 500 mm³; drei gemeinsame Grenzflächen. |
| Variable Wand und Schale | Keilförmiger Körper mit 625 mm³, lokale Schalentiefe 0,5–1,0 mm; geschlossener Restkörper. |
| Umlaufende Schale | Radius 3 mm, 64 Umfangssegmente, Länge 3 mm, Tiefe 0,25 mm; senkrechte Flächen und Überhänge ohne Höhenfeld. |
| Konvex/konkav | Feine und absichtlich zu grobe Netze gegen analytischen Zylinder. |
| Zwei Krümmungsrichtungen | Kugelpatch, Radius 5 mm, gegen analytische Kugel. |
| Enger Radius | Radius 0,15 mm: präzises Netz, aber unzureichende nominale Ablage bei 0,44 mm Linienbreite. |
| Hydrofoil | Analytisches NACA 0012 mit 100 mm Sehne, adaptive Kurve und umlaufende Ober-/Unterseiten-Topologie. Kein allgemeiner Vollwand-Offset an der scharfen Hinterkante. |
| Unterschiedliche Düsen/Linien | 0,8/0,4/0,6 mm Düsen und 0,88/0,44/0,66 mm Linien; Kontaktkompensation bei 15° Neigung und separate Rautiefen-/Krümmungsmessung. |
| Überdeckung und Volumenbilanz | Analytische Vereinigungsfläche je Bahnabstand gegen unabhängige numerische Integration für alle drei Profile; seitliche Lücken werden abgelehnt. Die Zahlen stehen im Prüfprotokoll. |
| Kontinuität | Ein Punktzug über gemeinsame Dreieckskante; kein doppelter Nahtpunkt. Kopfwechsel, auseinanderliegende Endpunkte und Punkte ausserhalb sehr schmaler Dreiecke werden abgelehnt. Positionsdifferenz am Testportal < 1e-12 mm. |
| Loch im Bereich | Quadratischer Ring; Schalenvolumen 8 mm³, Loch bleibt erhalten. |
| Profile und Zeit | Drei verschiedene Materialien gleichzeitig auf drei unabhängigen Testspuren; Positionen nach 0,1 s stimmen mit der jeweils eigenen, flussbegrenzten Geschwindigkeit auf 1e-9 mm überein. |
| Orca-Zuordnung | Düsenindex 2 und Materialindex 1 werden unabhängig aufgelöst; falscher Index wird abgelehnt. |
| Speicherung | Version 1 → 2, drei Profile über JSON und echte native 3MF gespeichert/wieder geladen; zukünftige Versionen bleiben geschützt. |
| Unterbau | Fehlende Fertigstellung, unbekannte Verträglichkeit, zu grosser Spalt und zyklische Abhängigkeiten werden abgelehnt. Keine Abstands-/Festigkeitsmessung aus einem realen Infill. |
| Maschinenhüllen | Vorwärtstransformation, Achs-/Hubgrenzen, unterschiedliche Hüllmasse, Kopf/Kopf- und Kopf/Bauteil-Bounding-Box-Kandidaten; TCP-Testabweichung < 1e-12 mm. |
| Ungültige Eingaben | NaN, offene Körper, umgeschlagene Offsets, zu grosse Schalentiefe, überlappende Bereiche und ungültige Prozessparameter werden abgelehnt. |

Für ID-Zuordnung, Speicherung, Graphen und Ablehnungstests ist ein geometrischer RMS-Wert nicht sinnvoll; dort werden die genannten logischen Eigenschaften geprüft. Es wird kein erfundener Nullfehler angegeben.

Ein zusätzlicher Regressionstest wurde zuerst tatsächlich fehlschlagend ausgeführt: Bei einem Dreieck von 1 mm Länge und 1e-8 mm Höhe konnte die Differenz nahezu gleicher Gram-Terme zu 0/0 werden und Punkte ausserhalb des Dreiecks durchlassen. Die Berechnung verwendet jetzt Kreuzprodukte und weist nicht endliche Koordinaten ausdrücklich zurück.

## Drei Materialien und Fertigungsabhängigkeiten

Jeder Kopf kann sein Orca-Materialprofil und einen eigenen aufgelösten Prozessdatensatz halten: Material-/Düsenprofil-ID, Durchmesser, Linienbreite, Schichthöhe, Temperatur, Geschwindigkeit, maximaler Volumenfluss, Flow sowie Schrumpfungs- und Querschnittskorrekturen. Der Adapter verlangt explizite Düsen- und Materialindizes. Ein Materialwechsel im vorhandenen Editor verwirft alte aufgelöste Werte, damit diese nicht für das neue Material verwendet werden.

Die projektbezogene Formorix-Struktur ist auf Version 2 angehoben. Bestehende Version-1-Projekte werden ohne erfundene Prozesswerte migriert. Die neuen Parameter sind über die C++-/JSON-Schnittstelle konfigurierbar und in 3MF speicherbar; es gibt noch keinen zusätzlichen GUI-Dialog für sämtliche neuen Maschinen- und Ablageparameter. Die vorhandene Orca-Profilverwaltung bleibt bestehen.

Bahnen enthalten Kopf, Fertigungsbereich, räumliche Pose, Orientierung und den zugehörigen vollständigen Prozessdatensatz. Synthetische Zeitspuren können gleichzeitig aktiv sein und unterschiedliche Geschwindigkeiten verwenden. Es gibt weder automatische Aufgabenteilung noch einen Scheduler. Eine unabhängige Bewegung in einer Testspur ist keine Freigabe für parallelen Druck.

Fertigungsoperationen unterscheiden Kern, verdichteten Übergang, Vollmaterial, wegbrechbare/wasserlösliche Stütze, Vollwand, Oberfläche und Verstärkung. Gerichtete Abhängigkeiten fordern den Abschluss des benötigten Unterbaus. „Infill first“ kann eine solche Abhängigkeit begründen, beweist aber keine ausreichende Auflage. Ein Infill-Prozentwert allein genügt nicht. Das Unterbau-Evidenzmodell fordert einen begrenzten Spalt über die gesamte Auflagefläche, Fertigstellung und explizite Materialverträglichkeit. Die automatische Ermittlung dieser Evidenz und mechanische Tragfähigkeitsprüfung fehlen noch.

## Maschinenmodell und verbleibende Grenzen

Drei Köpfe besitzen je einen parametrisierbaren XYZ-Bereich, Z-Rotation, Kippwinkel, Rotationszentrum und TCP-Hebelarm. Der gemeinsame Bett-Z-Hub wird getrennt geführt. Düsenkegel und Schaft werden konservativ durch Kapseln, das Umfeld durch eine Hüllkugel begrenzt. Alle Masse sind im Datenmodell veränderbar und standardmässig als Platzhalter markiert. Die Bounding-Box-Prüfung liefert nur mögliche Interaktionen, keine Kollisionsfreigabe und keine Prüfung kompletter Bewegungsvolumen.

Offen bleiben allgemeine glatte CAD-Offsets und robuste Vollwandzerlegung bei stark variabler Dicke, scharfen Kanten und sich schneidenden Normalen. Der Atlas unterstützt komplexe Topologie, wählt aber noch keine universelle kontinuierliche Flächenfüllung. Referenzbasierte Tests reparieren keine bereits ungenaue STL. Die lokale Kontaktkompensation allein bestätigt weder vollständige Oberflächenabdeckung noch ausreichende Materialmenge.

Die bestehenden Sperren für experimentelles Slicing und ausführbare Druckdateien bleiben aktiv. Es gibt keine Hybrid-Anbindung, automatische Druckreihenfolge, Mehrkopf-Bahnaufteilung, verwobenen Bahnen, automatische 5D-Supports, inverse Kinematik oder Klipper-Ansteuerung. Auftrag 04 wurde nicht begonnen.

## Build und Regression

Windows Release: Visual Studio 2026 / MSVC 14.51, vorhandene CMake-/CGAL-Abhängigkeiten. Der abschliessende vollständige Build verwendet zwei MSBuild-Jobs und acht Compilerprozesse je Job. Es wurden keine neuen Laufzeitabhängigkeiten eingeführt.

| Prüfung | Tatsächliches Ergebnis |
| --- | --- |
| Release `ALL_BUILD`, Anwendung und alle Tests | Erfolgreich. |
| Gezielte Grundlagen-/Projektprüfung | **26 Testfälle, 263 Assertions bestanden**; davon 18 neue Grundlagen-Testfälle. |
| Vollständige bisherige CTest-Auswahl | **3’110 bestanden, 6 übersprungen, 0 fehlgeschlagen**; 3’116 ausgewählt, 239,45 s. |
| Statische Prüfung gegen `origin/main` | `clang-tidy` für 14 betroffene Dateien erfolgreich. |
| Separate lokale Installation | `build/Formorix-Auftrag03B`, erfolgreich. |
| Programmstart | Installierte `orca-slicer.exe --help` mit isoliertem Datenverzeichnis, Prozessende abgewartet, Exit-Code 0. |
| Konventionelles Slicing / Export-Sperre | Bestehende FFF-Regressionen bestanden; der Vergleich identischer normaler Bewegungsbefehle enthält jetzt auch drei verschiedene Formorix-Prozessprofile. |

Kompakte Nachweise: [Build-/Teststatus](formorix/verification/auftrag-03b-checks.json), [gezieltes Testprotokoll](formorix/verification/auftrag-03b-unit.txt), [Formorix-Regressionsauszug](formorix/verification/auftrag-03b-regression.txt). Vollständige lokale Build- und CTest-Protokolle liegen zusätzlich im Arbeitsverzeichnis `../verification`.

Die sechs übersprungenen Tests benötigen Windows-Symlink-Rechte. Wie bisher werden `NotWorking` und `RequiresApp` ausgeschlossen; die Linux-CLI-Suite wird unter Windows nicht registriert. Die bekannte Upstream-CRT-Warnung `LNK4098` bleibt bestehen. Ein vollständiger manueller GUI-Durchlauf wurde für 03B nicht wiederholt; es gibt keine neuen GUI-Ansichten. Linux/macOS und reale Hardware wurden nicht getestet. Der lokale Build ist kein signierter Installer. Bitte den bereitgestellten Build zusätzlich manuell abnehmen.

Wenn ein vorhandenes Orca-Preset intern geändert wird, müssen neue Prozess-Snapshots später erneut aus dessen aktuellen Werten aufgelöst werden; dafür gibt es noch keine automatische Aktualisierung. Der Wechsel des zugewiesenen Materialnamens verwirft die alten Werte bereits.

Reproduktion aus dem Repository:

```powershell
cmake --build build --config Release --target ALL_BUILD --parallel 2 -- /p:CL_MPCount=2
build/tests/libslic3r/Release/libslic3r_tests.exe '[FormorixFoundations],[FormorixSurfaces]'
ctest --test-dir build/tests -C Release -LE 'NotWorking|RequiresApp' --no-tests=error --output-on-failure
scripts/run_clang_tidy.ps1 -Base origin/main -NoFetch -Jobs 1 -Yes
```

## Voraussetzungen für einen späteren Auftrag 04

Die Hybrid-Anbindung sollte die validierten Volumen und ihre gemeinsame Grenzfläche übernehmen, den konventionellen Kern mit Orcas bestehenden Verfahren planen und die dabei tatsächlich entstandene Auflage als Evidenz zurückführen. Vor einer allgemeinen 0,01-mm-Aussage braucht es zusätzlich CAD-Referenzbezug, eine vollständige Fehlerbilanz für das konkrete Bauteil, Flächenabdeckung und ein kalibriertes Ablagemodell. Maschinenmasse, Materialhaftung und thermische Randbedingungen müssen gemessen werden. Diese Arbeiten sind hier nur beschrieben, nicht implementiert.
