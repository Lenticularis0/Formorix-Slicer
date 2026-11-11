# Auftrag 05 – Räumliche Materialabdeckung

**Neun regionale Referenzen besitzen einen positiven nominalen Genauigkeitsnachweis bis 0,01 mm. Eine allgemeine Freigabe gekrümmter Hybridbauteile ist weiterhin nicht erreicht.** Gespeicherte Wölbung, vollständige Hydrofoil-Flächen und mehrere gekrümmte Referenzen bleiben diagnostisch oder werden begründet abgelehnt. Reale Druckgenauigkeit, Haftung, Tragfähigkeit und Kollisionsfreiheit sind nicht nachgewiesen.

Ausgangspunkt ist `main` mit Commit `983f285202ca349051809173a97f6154c56d7f09`, Entwicklungsbranch `formorix/auftrag-05`. Der Pull Request bleibt zur Prüfung offen. Die verlinkten Prüfungen wurden tatsächlich ausgeführt; die abschließende Release-/GUI-Abnahme wird separat ergänzt. Bestandene Negativtests bedeuten eine korrekte Ablehnung, keine unterstützte Fertigungsgeometrie.

## Implementierter Stand

| Bereich | Implementiert | Grenze |
| --- | --- | --- |
| Räumliches Material | Geschlossene Zellen aller extrudierenden Lagen; exakte CGAL-Vereinigung/Differenzen, Fehlmenge, Überstand, Mehrfachbelegung, Materialgrenzen und Kerneingriff; unabhängige Zufuhrbilanz. | Nominales, unkalibriertes Modell. Ungültige Zellen/Booleans und Arbeitslimits bleiben Fehler. |
| Randfüllung und Profile | Endliche ebene Rechteckzüge; lokale Breite nur innerhalb expliziter Limits. Drei unabhängige Düsen-, Material- und Prozessbindungen; getrennte Filament-/Prozess-/Oberflächenfaktoren. | Keine automatische Flowkorrektur oder beliebige gekrümmte Randfüllung. |
| Komplexe Oberflächen | Exakte 3D-Schnitte mit Originaldreiecksträgern, Löchern und geschlossenen Konturen; adaptive Normaleninterpolation; zyklische Gehrungen ohne künstlichen Neustart. | Konturen beweisen keine vollständige Materialabdeckung oder Erreichbarkeit. Verbindungen getrennter Züge bleiben nicht extrudierend und kollisionsungeprüft. |
| Scharfe Schalenränder | Symmetrische planare Gehrungen mit komplementärer innerer Neutriangulierung; Originalkontur und Regionsreihenfolge bleiben erhalten. | Variable Normalen/Tiefen, unvollständige gemeinsame Kanten und verbrauchte Innenflächen werden abgewiesen. |
| Nativer Unterbau | Proben auf tatsächlichen Kern-/Schalenschnittflächen; Bahnlücken, fehlende native Schichtlagen und unbekannte Querschnitte getrennt. Explizite Dichtewünsche erzeugen temporäre Orca-Modifier und eine echte Neuberechnung. | Keine vollständige Kontakt-/Festigkeitsfreigabe. Fertigungsabhängigkeiten bleiben unerledigt; Vollwand ohne Kern braucht eine unzugeordnete externe Tragstruktur. |
| Vorschau und Speicherung | Materialkörper/Defizite und ungeprüfte Zellen; drei Zeitspuren, Posen nur aus Bewegungsdaten; Schema 4 für Prozessbindungen/Breitenlimits und Invalidierung veralteter Profile. | Materialkörper zeigen alle Lagen unabhängig vom Zeitschieber. Der Ablauf bleibt 5D-only und nicht mit Orcas Kernbahnen synchronisiert. |

Details: [Materialprüfung](HLSD/FormorixMaterial.md), [Randfüllung](HLSD/FormorixBoundary.md), [Profile](HLSD/FormorixProfiles.md), [Konturen](HLSD/FormorixSurfacePaths.md), [Gehrungen](HLSD/FormorixFoundations.md#explicit-miter-boundaries), [Unterbau](HLSD/FormorixUnderlay.md), [Vorschau](formorix/material.md).

Die Materialprüfung verändert vor der Vereinigung keine Eingabekoordinaten. Ein begrenzter Ausgabefallback darf nur exakt koplanare Unterteilung ersetzen; beidseitig leere exakte Differenzen müssen denselben Volumenkörper einschließlich Hohlräumen beweisen. Die Abstandsprüfung begrenzt ganze Dreiecksflächen. Fehlerbudgetaddition rundet nach außen. Fehlende CAD-Referenz, unvollständige Abdeckung, unbestimmte Schranken oder widersprüchliche Materialbilanzen verhindern die Bestätigung. Fehlgeschlagene Audits liefern **unbekannte** Materialwerte, keine Nullfehler. In älteren archivierten Rohzeilen vorhandene Nullfelder bei einem Fehlerstatus sind zurückgesetzte Defaultwerte und keine Messung.

## Pflichtvergleich Auftrag 04 / Auftrag 05

Vergleichsbasis ist der geprüfte Auftrag-04-C++-Stand `2d5db079508df40f68a20de83942221bb8a67524`. Der [alte Geometriebericht](formorix/verification/auftrag-04-nachtrag/geometry.md) enthält drei positive und **14 negative regionale Diagnosezeilen**. Diese feste historische Kohorte ist keine Anzahl universell unterstützter Bauteile. Neue Referenzen oder Eingabereihenfolgen werden nicht zu einer Erfolgsquote dieser Kohorte addiert.

| Vergleich | Auftrag 04 | Auftrag 05 | Bewertung |
| --- | --- | --- | --- |
| Gespeicherte Wölbung, identische Vorgabe: Düse 0,4 mm, **Breite 0,42 mm**, vier Lagen à 0,2 mm, Flow 0,98 | Zufuhr/Schale 0,997321989773; äußere Projektionslücke 0,2607926873 %, Überstand 0,1032010617 %. | Zufuhr/Schale 0,997321989833; dieselben Projektionsanteile. Teilungsrest weiterhin 3,6379788071 × 10⁻¹² mm³. Räumlicher Audit: `ResourceLimit`, Sweep-Flächenbudget überschritten. | Keine behauptete Verbesserung der gekrümmten Materialabdeckung; räumliche Fehl-/Übermengen unbekannt. [Werte 05](formorix/verification/auftrag-05/package5-tests.txt), `native_curved_wall_standard04`. |
| Dieselbe Markierung, analytisches 0,8-/0,88-mm-Profil | Zufuhr/Schale 0,975396341311; Projektionslücke 2,4193280 %. | 0,975396341370; räumlicher Audit `InvalidSweep`. | Eigenständiges Profil, kein Ersatz für den 0,42-mm-Vergleich. |
| Hydrofoil-Negativfläche, bisheriger innerer Ausschnitt | Zufuhr/Schale 0,831208642920; Projektionslücke 16,2231432 %. | 0,831208643046; räumlicher Audit `InvalidSweep`. | Weiterhin unvollständig. Neue vollständige Hydrofoil-Tests stehen separat unten. |
| Kommensurable Ebene und zwei angrenzende Materialregionen | Positive ebene Abdeckungs-/Budgettests; Zufuhrfaktoren nahe 1. | Zusätzlich exakter räumlicher Nachweis aller Lagen, einschließlich innerer Fehlmengen, Mehrfachbelegung und Materialgrenzen. | Drei bestehende regionale Referenzen mit stärkerer räumlicher Evidenz. |
| Scharfe planare Gehrung mit umklappender innerer Feinreihe | Ablehnung. | Gültige komplementäre Innentriangulierung für geprüfte konstante planare Schalen; Volumenrest 0. | Geometrische Erweiterung, keine allgemeine geneigte Materialfreigabe. |

**Das aktuelle Orca-Autoprofil ist ein anderer Vergleichsfall:** Die neue Auflösung verwendet bei 0,4-mm-Düse etwa **0,45 mm** äußere Linienbreite statt des früheren pauschalen 0,42-mm-Wertes. Der native Unterbautest lädt das gespeicherte Prozessprofil und löst diese Breite frisch mit Orcas Flow-Funktion auf. Seine Ergebnisse sind kein unveränderter 0,42-mm-5D-Bahnvergleich. Die 22.400 historischen Auflagepunkte bleiben eine eigene RAW-Kohorte; die tatsächlichen Schnittflächen erhalten getrennte 36.496 Proben.

## Positive Referenzen und Gegenproben

| Regionale Referenz | Nominales Volumen | Genauigkeit und Umfang |
| --- | ---: | --- |
| Kommensurable Ebene (1 Region) | 3,6000000000000032 mm³ | Räumliches Maximum/Mittel/RMS 0 mm; Gesamtbudget < 9,637 × 10⁻⁸ mm. |
| Zwei benachbarte Materialien (2 Regionen) | Zusammen 4,5 mm³ | Beide regionalen Budgets < 9,637 × 10⁻⁸ mm; gemeinsame Materialgrenze erhalten. |
| Adaptives Rechteck 12 × 6,1 mm, zwei Lagen à 0,2 mm (1 Region) | 29,280000000000022 mm³ | Maximum/Mittel/RMS 0 mm; Budget < 2,528 × 10⁻⁷ mm. Lokale Breite ≈ 0,8054203673 mm innerhalb 0,74–0,84 mm. |
| Ebene mit Loch, zwei Lagen (1 Region) | 2,7000000000000024 mm³ | Maximum/Mittel/RMS 0 mm; vollständige Materialabstandsschranke ≈ 8,882 × 10⁻¹⁶ mm; regionales Gesamtbudget ≤ 0,01 mm. |
| Drei benachbarte Materialien, Düsen 0,8/0,4/0,6 mm (3 Regionen) | 12/9/18 mm³; 39 mm³ Schale und 321 mm³ Kern | Maximum/Mittel/RMS 0 mm; größtes regionales Budget 3,8247032 × 10⁻⁷ mm. Sechs Reihenfolgen bestanden, nicht mehrfach gezählt. |
| FH→Orca-Integration nahe X = 1600 mm (1 Region) | Kern 68,75 mm³; Volumen-Konvertierungsrest 0 | Haut-Maximum/Mittel/RMS 0 mm; Budget inklusive nativer Konvertierung **0,000047698814511611955 mm**. 80/272 Auflageproben fehlen; keine Gesamtbauteilfreigabe. |

Damit bestehen **neun unterschiedliche regionale nominale Referenznachweise**. Fehlmaterial, Außenmaterial, Mehrfachbelegung und Kerneingriff sind in den positiven räumlichen Referenzen jeweils 0. Die vollständigen Fehlerbeiträge und Modellbedingungen stehen in der [Abnahmematrix aller 15 Pflichtfamilien](formorix/verification/auftrag-05/acceptance-cases.md) und den [Dreimaterial-Prüfbedingungen](formorix/verification/auftrag-05/multi-material-tests.md).

Gegenproben bleiben negativ: feste Breite ohne Randanpassung lässt 0,24 mm³ fehlen; eine entfernte innere B-Lage lässt trotz korrekter äußerster Fläche 3 mm³ fehlen; eine unzulässig geneigte Zelle behält 0,52 mm³ Mangel, 0,12 mm³ Überstand und 0,4 mm³ Zufuhr-/Zellrest. Ein innerer Spalt von 2 × 10⁻¹⁰ mm wird nicht weggesnappt. Regionale Zufuhrfehler von etwa ±0,001 mm³ werden trotz fast verschwindendem globalem Rest nicht akzeptiert. Ein positiver Zusatz an der 0,01-mm-Grenze ergibt konservativ 0,010000000000000002 mm und verhindert die Freigabe. [Material-/Gehrungswerte](formorix/verification/auftrag-05/package4-tests.txt), [Genauigkeitsgates](formorix/verification/auftrag-05/package7-tests.txt).

Die drei Profile behalten eigene Lagenhöhen 0,25/0,125/0,1875 mm, Schalendicken 0,5/0,375/0,75 mm, Temperaturen, TCP-Versätze, Flows und Q-Limits 0,6/0,2/0,45 mm³/s. Jede reale Extrusion wird dagegen geprüft. Passende Materialgrenzen bestätigen keine Haftung; simultane Planung wird nicht behauptet.

## Tatsächlicher nativer Unterbau

| Explizite Strategie | RAW-Proben ohne nachgewiesenen Kontakt, vorher → nachher | Nominale native Zufuhr, vorher → nachher |
| --- | --- | --- |
| 80 % statt 20 %, 2.848 tatsächliche Schnittflächenproben | 1.527 → 585; flächengewichtet 79,38984375 → 19,04902344 mm² | 462,359413719 → 476,772650003 mm³ |
| 100 % statt 20 %, dieselben Proben | 1.527 → 1.860; 1.536 horizontale Proben wegen Brückenquerschnitten ungeprüft | 462,359413719 → 503,473837524 mm³ |
| Gespeicherte Wölbung, 22.400 historische RAW-Proben, frisches Orca-Autoprofil | 6.006 → 5.998 | 6.002,069392527 → 5.998,985844091 mm³ |
| Dieselbe Wölbung, 36.496 tatsächliche Schnittflächenproben | 10.135 → 10.130; flächengewichtet 365,087480966 → 364,859892101 mm² | Dieselben nativen Bahnen wie in der vorherigen Zeile |

Der 100-%-Fall erzeugt 86 interne Brückensegmente mit rundem 0,4-mm-Querschnitt. Dafür wird kein Stadiumkontakt erfunden; horizontale 98 mm² bleiben unbekannt. Die höhere Fehlkontaktzahl belegt keine physische Lückenvergrößerung. Die Software senkt die Benutzerdichte nicht automatisch auf 80 % ab. Bei beiden Strategien bleiben alle 464 geprüften Segmente unterhalb der angeforderten Schichten identisch; allgemeine Bahnidentität außerhalb der XY-Maske wird wegen Orcas Neuplanung nicht behauptet.

In der gespeicherten Wölbung bleiben **7.302 native Schichtlücken**, flächengewichtet 263,139356886 mm². Mehr Infill ersetzt keine fehlende Schichtlage. Bei unverändertem Flow 0,98 bleibt die strikte Materialauflage an sämtlichen Proben unbestätigt. Die historischen RAW-Zeilen blenden diese unkalibrierte Flow-Form ausdrücklich aus; sie sind keine geprüfte Materialauflage.

Die 25 gültigen Modifier bewahren Originalkern und unvergrößerte native Regionsvereinigung exakt. Maximale Float-Konvertierungsgrenze: 3,305773183 × 10⁻⁶ mm; maximaler Modifier-Volumenrest: 3,487799269 × 10⁻⁵ mm³. Orcas Sicherheitsversatz wird mit 3,141421356 × 10⁻⁵ mm getrennt bilanziert. Die Modifier-Offsetzulage 3,441421356 × 10⁻⁵ mm ist **keine** globale Hausdorff-Schranke für verschwundene dünne Maskenbereiche. Wunsch-/Anwendungsfläche über betroffene Lagen: 316,309275847/316,302975053 mm². Bei fehlgeschlagenem Modifierpass bleiben Baselinebahnen und Lücken erhalten. [Native Mengen-, Rollen- und Auflageprotokolle](formorix/verification/auftrag-05/package5-core.txt).

## Gekrümmte Flächen und Hydrofoil

| Referenz | Tatsächlicher Nachweis | Verbleibende Grenze |
| --- | --- | --- |
| Geneigte CAD-Ebene, konvexer/konkaver Zylinder, doppelt gekrümmte Kugel | Ganze Hybridpipeline ausgeführt. Import-Maxima der Zylinder 0,0015746191 mm, der Kugel 0,0031738632 mm. | Materialstatus jeweils `BooleanFailure` / `InvalidSweep` / `BooleanFailure` / `InvalidSweep`; kein vollständiger Materialnachweis, Gesamtbudget unendlich. |
| Vollständige 100-mm-NACA0012-Negativform mit Nase/Hinterkante | 756 Dreiecke, 6.048 euklidische Kurvenproben: Max/Mittel/RMS 0,001527982773 / 0,000164482776 / 0,000264095928 mm; unabhängige Kurvenschranke 0,001989831818 mm. | Reale 0,4-mm-Düse bei 75°: Ablehnung wegen bis zu 87,4805° Facettenneigung, keine Teilbahn. Kurvennachweis ist kein Materialnachweis. |
| Vollständige umlaufende analytische NACA-Kontur | Ein geschlossener Lauf mit 284 Knoten; Offset-Schranke 0,000695964662 mm. | Kontur-/Trägernachweis. Eine geometrische 180°-Diagnose ersetzt keine reale Maschinenfreigabe. |
| Unveränderte originale Hydrofoil-Negativform | 11.709 Vertices/23.414 Dreiecke; vollständige Negativauswahl 21.196 Dreiecke einschließlich 200 rücklaufender Facetten. Drei Schnitte: 336 Trägersegmente, Trägerfehler ≤ 1,173 × 10⁻¹³ mm, Offsetfehler ≤ 0,000163991646 mm, Schranke 0,000984353371 mm. | CAD-Referenz unbekannt; Normalen bis etwa 107°. Keine vollständige Flächen-/Materialabdeckung oder Erreichbarkeitsbestätigung. |

Die vollständige Originaldatei wurde nicht durch einen günstigen Ausschnitt ersetzt. [Originalmodell, Hash und Transformation](formorix/verification/auftrag-05/hydrofoil-input.md), [Konturprotokoll](formorix/verification/auftrag-05/package3-tests.txt), [vollständige analytische Abnahmefälle](formorix/verification/auftrag-05/package8-tests.txt).

## Ausgeführte automatisierte Prüfungen

Die archivierten Läufe überlappen sich; Fall- und Assertionzahlen werden **nicht addiert**. Sie belegen den protokollierten Stand und ersetzen nicht die finalen Paket-9-Prüfungen.

| Prüfung | Ausgeführtes Ergebnis und Protokoll |
| --- | --- |
| Breite Formorix-Regression nach nativer Unterbauintegration | 155 Fälle / 433.708 Assertions; native Orca-/Ausgabesperren zusätzlich 32 Fälle / 31.634 Assertions. [Geometrie/Modelle](formorix/verification/auftrag-05/package5-tests.txt), [native Integration](formorix/verification/auftrag-05/package5-core.txt). |
| Materialvorschau und Posen | 42 Fälle / 86.355 Assertions. [Protokoll](formorix/verification/auftrag-05/package6-tests.txt). |
| Verschärfte Genauigkeitsgates | 35 Fälle / 189.901 Assertions. [Protokoll](formorix/verification/auftrag-05/package7-tests.txt). |
| Zusätzliche Pflichtgeometrien und große Koordinaten | 3 geometrische Fälle / 111 Assertions; 1 nativer Fall / 689 Assertions. [Geometrie](formorix/verification/auftrag-05/package8-tests.txt), [Orca bei 1600 mm](formorix/verification/auftrag-05/package8-core.txt). |
| Frühere breite CTest-Regression des Vorschau-Stands | 3.267 bestanden, 6 Windows-Symlink-Skips, 0 Fehler, 290,69 s. **Kein** Endnachweis nach letzten Abnahme- und Übersetzungsänderungen. |
| Reihenfolge und Hilfe | Material-/Referenzfälle mit Seeds 7/1337 jeweils 22 Fälle / 855 Assertions; lokale Hilfe 13 Themen und 30 Datei-/Ankerprüfungen. [Seed 7](formorix/verification/auftrag-05/package1-order-7.txt), [Seed 1337](formorix/verification/auftrag-05/package1-order-1337.txt), [Hilfe](formorix/verification/auftrag-05/package6-help-local.json). |

Die neueren gezielten Läufe verwenden Seed 42. Tests decken innere Lücken, Limits, Abbruch, leere/ungültige Ablage, verlorene Topologie, Materialidentität, unbekannte CAD-Referenzen und ungültige/veraltete Profile ab. Automatische Vollwandauflage, Kinematik, Kollisionsfreigabe, Mehrkopf-Scheduler, Klipper oder ausführbare 5D-/Hybriddateien wurden nicht implementiert. Die experimentellen Export-, Cache- und Sendesperren bleiben erhalten.

## Performancevergleich: Abschlussmessung ausstehend

Die Auftrag-04-Basis wurde vor dem ersten Auftrag-05-Build erneut gemessen, Seed 42. Laufzeit umfasst Prozessstart und Assertions; Speicher ist der alle 50 ms abgefragte Peak Working Set, keine isolierte Kernelmessung.

| Identische Basistest-Auswahl | Auftrag 04 Laufzeit | Beobachteter Peak Working Set |
| --- | ---: | ---: |
| Gespeicherte Wölbung, beide getrennten Testprofile | 8,3289462 s | 77.426.688 Byte |
| Geneigte/konvexe/konkave/Hydrofoil-Ausschnitte | 0,253531 s | 25.038.848 Byte |
| Kommensurable Ebene | 0,1205067 s | 15.228.928 Byte |

[Basismessung](formorix/verification/auftrag-05/baseline-performance.json). Ein entsprechend abgegrenzter Auftrag-05-Endlauf steht aus; Beschleunigung oder unveränderter Speicherbedarf werden nicht behauptet. Einzelne Testdauern sind kein kontrollierter Vergleich. Ein früher Diagnoseprozess wurde nach über acht CPU-Minuten beendet und nicht als bestanden gewertet. Das Materialaudit besitzt nun ein kooperatives 60-Sekunden-Budget; einzelne nicht unterbrechbare CGAL-Operationen können es überschreiten.

## Finale Release- und GUI-Abnahme – noch zu ergänzen

**Diese Tabelle bleibt bis zur tatsächlich abgeschlossenen Prüfung offen.** Frühere erfolgreiche Builds oder Teiltests übertragen ihren Status nicht automatisch auf den endgültigen Quellstand.

| Endprüfung | Abschlussstatus |
| --- | --- |
| Finaler Quellcommit, Windows-Release-Build und Installationsartefakt mit SHA256 | Ausstehend; Commit, Buildlog und Artefaktmanifest ergänzen. |
| Vollständige finale CTest-Auswahl | Ausstehend; ausgewählt/bestanden/übersprungen/fehlgeschlagen, Ausschlüsse, Dauer und Log ergänzen. |
| clang-tidy gegen `origin/main` | Ausstehend; endgültigen Dateiumfang und Ergebnis ergänzen. |
| gettext, deutsche Kataloge und veröffentlichte Hilfeziele | Abschlussnachweis ausstehend; Katalogprüfung, unveränderlichen Hilfe-Pin und HTTP-/Ankerprotokoll ergänzen. |
| Native GUI: Materialanzeigen, drei Kopfzustände, Zeitleiste, Abbruch und veraltete Vorschau | Ausstehend; tatsächlich bediente Abläufe und Screenshots ergänzen. |
| Projekt-/GUI-Roundtrip und Rückkehr zum normalen Orca-Slicing | Abschlussnachweis ausstehend; Bereiche/Profile/Breitenlimits und weiterhin gesperrten experimentellen Export dokumentieren. |
| Kontrollierter Performancevergleich 04/05 | Ausstehend; gleiche Modelle/Profile und getrennte Erfolgs-/Abbruchzustände verwenden. |

Andere Betriebssysteme und reale Druckhardware sind nicht abgenommen. Der Stand ist ein besser überprüfbarer experimenteller Prototyp; die verbleibenden Grenzen der gekrümmten Materialabdeckung erlauben keine Behauptung, Auftrag 05 sei uneingeschränkt erfüllt.
