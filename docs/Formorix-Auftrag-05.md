# Auftrag 05 – Räumliche Materialabdeckung

Ausgangspunkt ist `main` mit Commit `983f285202ca349051809173a97f6154c56d7f09`. Vor Anlage von `formorix/auftrag-05` wurde der Remote-Stand aktualisiert; es gab keine neueren Main-Commits. Der Pull Request wird zur Prüfung offen gelassen.

Dieser Bericht erfasst ausgeführte Prüfungen. Noch nicht geprüfte Arbeitsstände erhalten keinen Erfolgsstatus. Das Materialmodell bleibt nominal und unkalibriert; eine geometrische Bestätigung ist keine Aussage über reale Druckgenauigkeit, Haftung oder Kollisionsfreiheit.

## Räumliche Materialprüfung

`FormorixMaterial` bildet tatsächliche extrudierende Bewegungen aller Lagen als geschlossene räumliche Zellen ab. Gemeinsame Gehrungsquerschnitte vermeiden künstliche Endkappen innerhalb eines Extrusionszugs. Exakte CGAL-Operationen bilanzieren Vereinigung, fehlendes Material, Überstand, Mehrfachbelegung, regionale Materialüberschneidung und Eindringen in den Kern. Die Zufuhr aus der Zeitleiste wird getrennt mit der geometrischen Zellmenge verglichen. [Architektur und Nachweisgrenzen](HLSD/FormorixMaterial.md).

Die vor der Vereinigung unveränderten binären Koordinaten sind maßgeblich. Eine nachträgliche Rasterung darf keine inneren Lücken verstecken. Die räumliche Abstandsprüfung begrenzt ganze Dreiecksflächen; Stichprobenmittelwerte allein erlauben keine Freigabe. Die äußere nominelle Zellfläche und die vollständige Volumengrenze werden getrennt geprüft.

## Vergleichsgrundlage

Die folgenden Messungen stammen aus dem unveränderten, für Auftrag 04 geprüften Windows-Release-Testprogramm mit C++-Quellstand `2d5db079508df40f68a20de83942221bb8a67524`. Sie wurden vor dem ersten Auftrag-05-Build erneut ausgeführt, mit Zufallsseed 42. Laufzeiten schließen Prozessstart und Testassertions ein. Speicherwerte sind im Abstand von 50 ms abgefragte Prozess-Spitzenwerte, keine isolierten Kernelmessungen.

| Basistest | Ergebnis | Laufzeit | Beobachteter Peak Working Set |
| --- | --- | ---: | ---: |
| Gespeicherte Wölbung, reale Markierung | bestanden; 150.655 Assertions | 8,3289462 s | 77.426.688 Byte |
| Geneigte, konvexe, konkave und Hydrofoil-Negativfläche | bestanden | 0,253531 s | 25.038.848 Byte |
| Kommensurable Ebene | bestanden; 213 Assertions | 0,1205067 s | 15.228.928 Byte |

Die alten gekrümmten Materialanteile sind ausdrücklich Projektionsdiagnostik und dürfen nicht als räumliche Referenzvolumen gelesen werden. Die vollständigen alten Resultate stehen im [Bericht zu Auftrag 04](Formorix-Auftrag-04-Nachtrag.md).

## Geprüfter Zwischenstand: räumliches Materialmodul

Windows x64 Release mit dem vorhandenen MSVC-/CGAL-Build: Material-, Grundlagen-, Hybrid- und Vorschautests gemeinsam in zufälliger Reihenfolge ausgeführt (`--order rand --rng-seed 42`). **81 Fälle / 221.757 Assertions bestanden.** Zusätzlich bestanden Materialmodul und positive Hybrid-Referenzen mit Seeds 7 und 1337 jeweils **22 Fälle / 855 Assertions**. [Vollständige Messwerte](formorix/verification/auftrag-05/package1-tests.txt), [Reihenfolge 7](formorix/verification/auftrag-05/package1-order-7.txt), [Reihenfolge 1337](formorix/verification/auftrag-05/package1-order-1337.txt).

| Referenz | Räumliches Ergebnis | Status |
| --- | --- | --- |
| Kommensurable Ebene, sämtliche Lagen | Vereinigung 3,6000000000000032 mm³; Fehlvolumen, Überstand, Mehrfachbelegung und Kerneingriff jeweils 0 mm³; gemessenes Maximum/Mittel/RMS jeweils 0 mm | Nominale räumliche Identität nachgewiesen; CAD-Fehlerbudget ≤ 0,01 mm. |
| Zwei angrenzende Materialien mit verschiedenen Düsen | Vereinigung 4,5 mm³; Fehlvolumen, Überstand, Mehrfachbelegung und Kerneingriff jeweils 0 mm³; beide regionalen äußeren Abstandsschranken 0 mm | Drei regionale positive Referenznachweise einschließlich der einzelnen Ebene; keine Haftungsfreigabe. |
| Eigenständige gekrümmte Sektorzelle | Analytisches Polyedervolumen 0,64 mm³ bestätigt; unabhängige Zufuhrabweichung bleibt ausgewiesen | Geometrischer Einzeltest, kein vollständiger gekrümmter Hybridnachweis. |
| Innerer Spalt von 2 × 10⁻¹⁰ mm | Fehlvolumen bleibt erhalten; vollständige Abdeckung wird verweigert | Negativtest bestanden; kein Wegschnappen der Lücke. |
| Gespeicherte Wölbung und weitere gekrümmte Hybridfälle | Ungültige Sweep-Zellen, nichtmannigfaltige Vereinigungen oder Arbeitslimits verhindern derzeit vollständige räumliche Evidenz | Noch offen; alte Projektionswerte bleiben ausschließlich diagnostisch. |

Die konservativ addierte Fehlerschranke der drei ebenen Referenzregionen liegt jeweils unter **9,637 × 10⁻⁸ mm**, einschließlich der erfassten Float-Konvertierung des Kerns. Das ist ein Nachweis der definierten nominalen Schale, kein Nachweis realer Druckflächen oder sämtlicher konventioneller Kernbahnen. Die Materialzufuhr wurde nicht nachträglich auf das Zielvolumen normiert. Der größte Betrag des Zufuhr-/Zellvolumenrests der ebenen Referenzen beträgt rund 2,67 × 10⁻¹⁵ mm³. Gemeinsame Schichtgrenzen, eine algebraisch unveränderte stabilere Stadiumformel und genauer auswertbare ebene Raster beseitigen rechnerisch erzeugte Spalte beziehungsweise umgeklappte Gehrungen. Originalmesh und konfigurierte Flow-Faktoren bleiben unverändert.

Die Regression deckt außerdem Abbruch, Eingabe-/Arbeitslimits, fehlende Stichproben, leere Ablage, ungültige Zwischenpunkte, unterschiedliche Materialien, Kernintrusion und unzureichend darstellbare Boolean-Ergebnisse ab. Gefundene Stack- und Zugriffsfehler sowie eine nicht initialisierte CGAL-Hilfsgröße wurden vor diesem Prüflauf korrigiert. Ein Topologie verändernder Schalen-Ausgabefallback erhält keine pauschale Genauigkeitsbestätigung.

## Geprüfter Zwischenstand: adaptive Randfüllung und Profile

Die Randfüllung unterstützt ein ebenes Rechteck mit endlichen Gehrungen und gemeinsamen Seitenkoordinaten. Aus ausdrücklich angegebenen Breitenlimits bestimmt sie eine passende Anzahl von Bahnen und deren lokale Querschnitte. Ohne Limits bleiben Reststreifen sichtbar. Prozessprofile, Düse und Material sind je Kopf unabhängig; Filament-, Druck- und Außenwandfaktoren bleiben getrennt erhalten. Die native Auto-Linienbreite verwendet Orcas vorhandene Auflösung. Projekt-Schema 4 speichert optionale Prozessbindungen und Breitenlimits; alte Projekte erhalten keine erfundenen Breitenfreigaben. [Modul und Grenzen](HLSD/FormorixBoundary.md).

Die gezielte Windows-Release-Regression besteht mit **113 Fällen / 220.131 Assertions**; zusätzlich bestehen **20 native Orca-/Exportsperrenfälle / 9.111 Assertions**. [Geometrie, Profile und Speicherung](formorix/verification/auftrag-05/package2-tests.txt), [native Kernintegration](formorix/verification/auftrag-05/package2-core.txt). Die kompaktere ebene Bahn benötigt weniger redundante Stationsassertions. Der Ressourcentest setzt seine Obergrenze nun ausdrücklich und prüft weiterhin die Ablehnung statt von der früheren Punktunterteilung abzuhängen. Die feste maximale Bewegungsgrenze bleibt unverändert.

| Referenz | Tatsächliches Ergebnis |
| --- | --- |
| 12 × 6,1 mm, zwei Lagen à 0,2 mm | Acht Reihen je Lage; kompaktierte Breite 0,7625 mm; angeforderte lokale Breite etwa 0,8054203673 mm innerhalb der deklarierten Grenzen 0,74–0,84 mm. Räumliches Materialvolumen 29,280000000000022 mm³, Zufuhr 29,280000000000001 mm³. Fehlvolumen, Überstand, Mehrfachbelegung und Kerneingriff jeweils exakt 0 mm³. |
| Vollständige Fehlerbilanz dieses Rechtecks | Gemessenes Maximum/Mittel/RMS 0 mm; konservative Gesamtschranke unter 2,528 × 10⁻⁷ mm. Positive nominale CAD-Bestätigung, keine physische Druckfreigabe. |
| Feste Breite ohne erlaubte Anpassung | Im integrierten Test fehlen 0,24000000000000044 mm³; keine Überextrusion, kein Überstand, keine Genauigkeitsbestätigung. Beobachtetes räumliches Maximum 0,10000000000000009 mm; vollständige Abstandsschranke und Mittel/RMS bleiben bei ausgeschöpftem Arbeitsbudget unbekannt. |
| Unterschiedliche Lagenhöhen und große Koordinaten | Das eigenständige Randmodul vereinigt 0,2-/0,1-mm-Lagen auf X = 1588–1600 mm lücken- und überlappungsfrei. |

Ein zunächst ausgeschöpftes Vergleichsbudget beim fein triangulierten Rechteck wurde durch den vorhandenen exakten Vergleich koplanarer Flächen gelöst. Dessen zulässige Eingabegröße wurde erweitert; die kombinatorische Arbeitsgrenze und die Genauigkeitsregeln bleiben aktiv. Vier regionale Hybrid-Referenzen besitzen nun einen positiven nominalen Gesamtnachweis.

Die native Orca-Berechnung trennt rohe Geometrie und erst beim G-Code-Export angewendete statische Materialfaktoren. Nullzufuhr kann keine Auflage liefern. Bei verändertem Flow bleibt die daraus tatsächlich entstehende Strangform ungeprüft; unveränderte rohe Kernpfade bleiben für geometrische Vergleiche verfügbar. Die neuen nativen Tests prüfen getrennte Rollen-, Erstschicht- und Supportfaktoren sowie die Volumenstromgrenze.

Die weiteren Arbeitspakete, native GUI-Abnahme, vollständige CTest-Regression, gettext, clang-tidy und vollständiger Projekt-/GUI-Roundtrip sind noch nicht abgeschlossen und werden erst nach tatsächlicher Durchführung eingetragen.

## Geprüfter Zwischenstand: räumliche Konturen

Das neue [Konturmodul](HLSD/FormorixSurfacePaths.md) schneidet das unveränderte Dreiecksnetz mit exakten CGAL-Ebenen. Jeder Abschnitt besitzt einen nachgewiesenen Träger im Originaldreieck. Damit lassen sich auch Löcher, zurücklaufende Konturen und geschlossene umlaufende Profile darstellen. Die bestehende zusammenhängende Rasterführung bleibt für unterstützte Höhenfelder erhalten. Die räumliche Erweiterung greift bei zuvor abgelehnten Topologien; ein dort nicht unterstützter positiver Randabstand wird ausdrücklich abgelehnt. Getrennte Konturen erhalten echte, nicht extrudierende und weiterhin kollisionsungeprüfte Verfahrbewegungen.

Geschlossene Materialzüge besitzen zyklische Gehrungen ohne künstlichen Endabschluss. Adaptive Unterteilung begrenzt die Interpolation des definierten Normalenfelds über ganze Segmente. Dies bestätigt weder eine unbekannte CAD-Fläche noch automatisch die vollständige gekrümmte Materialabdeckung. Das Materialmodul besitzt zusätzlich ein weiches 60-Sekunden-Arbeitsbudget; einzelne CGAL-Operationen können dieses überschreiten. Eine abgebrochene Prüfung behält keine akzeptierte Geometrieevidenz.

Windows-Release-Build der beiden Testsuiten erfolgreich. Die gezielte Regression besteht mit **133 Fällen / 405.006 Assertions**; die native Kernintegration und Ausgabesperren bestehen mit **20 Fällen / 9.111 Assertions**, jeweils Seed 42. [Geometrie und Konturen](formorix/verification/auftrag-05/package3-tests.txt), [native Kernintegration](formorix/verification/auftrag-05/package3-core.txt). Ein früherer Versuchsstand wurde wegen einer über acht CPU-Minuten dauernden Materialprüfung beendet und zählt ausdrücklich nicht als bestanden. Die endgültige Auswahl der Konturführung vermeidet diese Regression.

| Referenz | Tatsächliche Messung und Grenze |
| --- | --- |
| Ebene mit Loch, zwei vollständige Lagen | Vereinigung 2,7000000000000024 mm³; Zufuhr 2,7000000000000002 mm³; Fehlvolumen, Überstand, Mehrfachbelegung und Kerneingriff 0 mm³. Maximum/Mittel/RMS 0 mm, vollständige räumliche Abstandsschranke 8,882 × 10⁻¹⁶ mm. Positiver nominaler CAD-Nachweis. |
| Doppelt gekrümmte Kontur | 189 Knoten in drei Zügen; Interpolationsfehler Maximum 0,000032975706 mm, Mittel 0,000012320233 mm, RMS 0,000014840696 mm; konservative Segmentschranke 0,000197853287 mm. Konturprüfung, keine vollständige Materialfreigabe. |
| Umlaufender Zylinder | 384 Knoten in zwei geschlossenen Konturen; Maximum 0,000050895702 mm, Mittel 0,000026956103 mm, RMS 0,000031421375 mm; Schranke 0,000305451933 mm. Im integrierten Hybridfall verhindert eine nichtmannigfaltige Materialdifferenz weiterhin die Gesamtbestätigung. |
| Vollständige analytische NACA-Kontur | 284 Knoten einschließlich Nasen- und Hinterkante; Maximum 0,000115588395 mm, Mittel 0,000008258948 mm, RMS 0,000021556334 mm; Schranke 0,000695964662 mm. Kontur- statt Volumennachweis. |
| Originale Hydrofoil-Negativform | Native 3MF-Einlesung und unveränderte Quelldatei nachgewiesen. Drei vollständige Schnitte enthalten 336 Segmente, darunter fünf zurücklaufende Abschnitte. Trägerabweichung ≤ 1,173 × 10⁻¹³ mm; gemessener Offsetfehler ≤ 0,000163991646 mm, konservative Schranke 0,000984353371 mm. Keine vollständige Flächenabdeckung oder CAD-Genauigkeitsbestätigung. |

Die [Analyse des tatsächlichen Hydrofoil-Modells](formorix/verification/auftrag-05/hydrofoil-input.md) beschreibt Auswahl, Transformation, Hash und Importfehler getrennt. Seine vollständige markierte Negativfläche enthält Normale bis etwa 107°; daraus folgt keine Erreichbarkeit mit dem realen 75°-Kippbereich. Umlaufende geometrische Tests verwenden ausdrücklich analytische Winkelgrenzen und sind keine Maschinenfreigabe.

Insgesamt besitzen nun **fünf regionale nominale Hybrid-Referenzen** einen vollständigen positiven Fehlerbudgetnachweis: eine kommensurable Ebene, zwei benachbarte Materialregionen, ein adaptiv gefülltes Rechteck und die Ebene mit Loch. Das zählt Regionen, nicht physische Druckversuche oder vollständig verifizierte konventionelle Bauteile.

## Geprüfter Zwischenstand: drei Materialien und innere Gehrungen

Drei unmittelbar benachbarte Bereiche eines Werkteils werden mit unabhängigen 0,8-/0,4-/0,6-mm-Düsen, Materialien A/B/C, Lagenhöhen, Tiefen, Temperaturen, TCP-Abständen, Volumenstromgrenzen und drei getrennten Flowfaktoren berechnet. Alle sechs Eingabereihenfolgen liefern dieselbe Materialaufteilung. Die [Prüfkonfiguration](formorix/verification/auftrag-05/multi-material-tests.md) nennt sämtliche Werte. Weder Flow noch Originalprofile werden auf das gewünschte Volumen normiert.

Bei exakt planaren Nachbarflächen kann CGAL umklappende innere Feinreihen durch komplementäre Gehrungsschnitte neu triangulieren. Die ursprünglichen äußeren Dreiecke bleiben erhalten. Variable Normalen/Tiefen, nur teilweise passende gemeinsame Kanten und aufgebrauchte innere Flächen werden weiterhin abgewiesen. Zwei- und Dreiseiten-Ecken sowie geänderte Bereichsreihenfolgen sind geprüft. [Konstruktionsgrenzen](HLSD/FormorixFoundations.md#explicit-miter-boundaries).

| Prüfung | Tatsächlich gemessen |
| --- | --- |
| Drei angrenzende Materialregionen, sämtliche Lagen | A/B/C: 12/9/18 mm³, zusammen 39 mm³; Kern 321 mm³. Fehlvolumen, Überstand, Mehrfachbelegung und Kerneingriff jeweils 0 mm³. |
| Nominale Genauigkeit der drei Ebenen | Äußeres Maximum/Mittel/RMS jeweils 0 mm; größtes zusammengesetztes Fehlerbudget **3,8247032 × 10⁻⁷ mm**. Alle drei regionalen Gates positiv; Haftung und physischer Druck bleiben ungeprüft. |
| Fehlende innere B-Lage | Exakt 3 mm³ Mangel trotz weiterhin passender äußerer Flächen. Globale beobachtete Abstandsunterschranke etwa 0,125 mm; vollständige obere Schranke bei erschöpftem Abstandsbudget unbekannt. Keine Genauigkeitsfreigabe. |
| Zweifache scharfe Gehrung mit innerer Feinreihe 0,1 beziehungsweise 0,01 mm | Top/Front/Kern: 57/47/396 mm³; Volumenrest 0. Rundungsschranke ≤ 1,561 × 10⁻¹⁵ mm. Acht Varianten bestanden. |
| Drei rechtwinklige Gehrungsregionen | 55,6/45,85/22,35 mm³; Kern 376,2 mm³; Volumenrest 0. Rundungsschranke ≤ 1,951 × 10⁻¹⁵ mm. Beide Reihenfolgen bestanden. |
| Geneigte, unzulässig abgelegte Materialzelle | Fehlmenge 0,52 mm³, Überstand 0,12 mm³, Zufuhr-/Zellrest 0,4 mm³ bleiben auch in den ausgegebenen Körpern erhalten. Weiterhin keine vollständige Abdeckung oder Abstandsbestätigung. |

Der Innenlagen-Negativtest deckte einen zusätzlichen Ausgabefehler auf: exakte Boolean-Hilfsdreiecke konnten beim Runden kollabieren. Der begrenzte [Material-Ausgabefallback](HLSD/FormorixMaterial.md) entfernt ausschließlich überflüssige koplanare Unterteilung. Beidseitig leere exakte Differenzen beweisen zuvor, dass sich der vollständige Volumenkörper einschließlich seiner Hohlräume nicht ändert. Die fehlende Lage und die unzulässige geneigte Ablage werden deshalb präziser diagnostiziert, nicht als zulässig umgedeutet. Das unveränderte Abstandsbudget reserviert jetzt bei mehreren Regionen einen Anteil für den globalen Vergleich.

Der abschließende Windows-Release-Prüflauf besteht mit **139 Fällen / 433.016 Assertions**, zusätzlich **20 nativen Orca-/Ausgabesperrenfällen / 9.111 Assertions**, Seed 42. [Vollständige Geometrieergebnisse](formorix/verification/auftrag-05/package4-tests.txt), [native Regression](formorix/verification/auftrag-05/package4-core.txt). Frühere fehlgeschlagene Entwicklungsprüfungen zählen nicht als bestanden. Insgesamt bestehen jetzt **acht regionale positive nominale Hybrid-Nachweise**; die sechs Reihenfolgen werden dabei nicht mehrfach gezählt.

## Geprüfter Zwischenstand: nativer Unterbau

Das [Unterbaumodul](HLSD/FormorixUnderlay.md) prüft flächengewichtete Proben auf der tatsächlich berechneten Kern-/Schalengrenze. Es unterscheidet echte Bahnlücken, unerreichbare native Schichtlagen und unbekannte Querschnitte. Nur eine ausdrücklich aktivierte Verdichtungsoption erzeugt lokale Dichtewünsche. Orca berechnet diese über temporäre Modifier; alle übrigen Profile und Flow-Faktoren bleiben erhalten. Anschließend werden die tatsächlichen neuen Bahnen geprüft. Fertigungsabhängigkeiten bleiben unerledigt; eine Vollwand ohne Kern bekommt eine ausdrücklich unzugeordnete externe Voraussetzung.

| Explizite Teststrategie | Rohe Proben ohne nachgewiesenen Kontakt, vorher → nachher | Nominale native Materialzufuhr, vorher → nachher |
| --- | --- | --- |
| Lokale 80-%-Verdichtung, 2.848 Grenzflächenproben | 1.527 → 585; flächengewichtet 79,3898437500 → 19,0490234375 mm² | 462,359413719 → 476,772650003 mm³ |
| Lokale 100-%-Verdichtung, dieselben Proben | 1.527 → 1.860; alle 1.536 horizontalen Proben werden wegen nativer Brückenquerschnitte ungeprüft | 462,359413719 → 503,473837524 mm³ |
| Gespeicherte Wölbung, unveränderte 22.400 historische RAW-Proben | 6.006 → 5.998 | 6.002,069392527 → 5.998,985844091 mm³ |
| Dieselbe Wölbung, 36.496 tatsächliche Grenzflächenproben | 10.135 → 10.130; flächengewichtet 365,087480966 → 364,859892101 mm² | Dieselben nativen Bahnen wie in der vorherigen Zeile |

Der 100-%-Fall erzeugt 86 native interne Brückensegmente mit rundem 0,4-mm-Querschnitt. Für diese wird kein Stadiumkontakt erfunden. Die gesamte horizontale Auflage von 98 mm² bleibt ungeprüft; ihr höherer Fehlkontaktzähler ist keine nachgewiesene physische Lückenvergrößerung. Der gezielte Negativtest verhindert eine falsche Freigabe. Die Benutzerdichte wird nicht automatisch abgesenkt. Der separate positive 80-%-Fall verwendet eine ausdrücklich andere Testvorgabe. Bei beiden bleiben alle 464 geprüften Segmente unterhalb der angeforderten Schichten identisch.

Eine neue native Füllberechnung kann außerhalb der lokalen XY-Dichtezone andere Bahnen erzeugen. Im 100-%-Fall ändert sich die nominale Zufuhr bei X ≥ 14 mm von 151,981698211 auf 151,798913301 mm³. Eine allgemeine Identität aller dortigen Bahnen wird nicht behauptet. Bei der gespeicherten Wölbung bleiben **7.302 native Schichtlücken**, flächengewichtet 263,139356886 mm². Mehr Infill kann diese fehlende Schichtgeometrie nicht ersetzen. Der unveränderte Flow 0,98 lässt die strikte Materialauflage an sämtlichen Proben weiterhin unbestätigt.

Die Wölbung verwendet 25 gültige lokale Modifier. Maximale Float-Konvertierungsschranke: **3,305773183 × 10⁻⁶ mm**; maximaler Modifier-Volumenrest: **3,487799269 × 10⁻⁵ mm³**. Orcas separater Sicherheitsversatz besitzt eine konstruktive Ausdehnungsschranke von **3,141421356 × 10⁻⁵ mm**. Die konservative Innenbegrenzung der Dichtezone verwendet eine Offsetzulage von **3,441421356 × 10⁻⁵ mm**. Sie ist keine globale Hausdorff-Schranke für entfernte dünne Maskenbereiche. Über betroffene Schichten summierte Wunsch-/Anwendungsflächen: 316,309275847 / 316,302975053 mm². Originalkern und unvergrößerte native Regionsvereinigung bleiben exakt erhalten.

Die gemeinsamen Randindizes der Modifier-Kappen und -Seiten beseitigen zuvor ungültige Netze. Orcas vorhandener Sicherheitsversatz bei mehreren Prozessregionen wird getrennt erfasst und gegen dieselbe native Operation geprüft. Die ursprünglichen fehlgeschlagenen Entwicklungsprüfungen wurden nicht als bestanden gewertet. Die GUI bewahrt bei einem verworfenen Modifierpass die tatsächlichen Baselinebahnen und ihre Lücken; die neue Analyse bleibt ausdrücklich unvollständig. Die vollständige GUI-Abnahme steht noch aus.

Der abschließende Windows-Release-Build beider Testsuiten ist erfolgreich. Die gesamte bisherige gezielte Formorix-Regression besteht mit **155 Fällen / 433.708 Assertions** sowie **32 nativen Orca-/Ausgabesperrenfällen / 31.634 Assertions**, jeweils zufällige Reihenfolge mit Seed 42. [Geometrie, Profile und Unterbaumodell](formorix/verification/auftrag-05/package5-tests.txt), [native Berechnung einschließlich Mengen und Brückennegativtest](formorix/verification/auftrag-05/package5-core.txt). Vierzehn eigenständige Unterbautests prüfen unter anderem Flächengewichte, Löcher, senkrechte Interfaces, Raft-Z, ausdrücklich ausgeschlossenen Infill, unzugeordnete Vollwand-Voraussetzungen, Ressourcenlimits und veraltete Kontakte.
