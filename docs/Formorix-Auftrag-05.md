# Auftrag 05 – Räumliche Materialabdeckung

**Neun regionale Referenzen besitzen einen positiven nominalen Genauigkeitsnachweis bis 0,01 mm. Eine allgemeine Freigabe gekrümmter Hybridbauteile ist weiterhin nicht erreicht.** Gespeicherte Wölbung, vollständige Hydrofoil-Flächen und mehrere gekrümmte Referenzen bleiben diagnostisch oder werden begründet abgelehnt. Reale Druckgenauigkeit, Haftung, Tragfähigkeit und Kollisionsfreiheit sind nicht nachgewiesen.

Ausgangspunkt ist `main` mit Commit `983f285202ca349051809173a97f6154c56d7f09`, Entwicklungsbranch `formorix/auftrag-05`. Der finale C++-Stand `b066c16391dbee033abbd8a03f7379c6ff9394ac` wurde als Windows-Release erfolgreich gebaut und installiert. Automatisierte Regression, clang-tidy, Katalog-/Hilfeprüfungen, Performancevergleich und die [dokumentierten nativen GUI-Prüfungen](formorix/verification/auftrag-05/gui-verification.md) sind abgeschlossen. Die dort benannten ungeprüften Bedienfälle bleiben offen. Der Pull Request bleibt zur Prüfung offen. Bestandene Negativtests bedeuten eine korrekte Ablehnung, keine unterstützte Fertigungsgeometrie.

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

Vergleichsbasis ist der geprüfte Auftrag-04-C++-Stand `2d5db079508df40f68a20de83942221bb8a67524`. Die feste historische Kohorte besteht aus 17 numerischen regionalen `HYBRID`-Zeilen, einschließlich vier getrennter `complete_top_boundary`-Fälle. Alle haben im Auftrag-05-Protokoll ein Gegenstück: **in beiden Ständen drei positiv und 14 nicht positiv**, ohne Statuswechsel. Textuelle Vorab-Ablehnungen und neue Referenzen zählen nicht zu dieser Kohorte. [Werte 04](formorix/verification/auftrag-04-nachtrag/geometry-tests-final.txt), [Werte 05](formorix/verification/auftrag-05/package7-tests.txt).

| Vergleich | Auftrag 04 | Auftrag 05 | Bewertung |
| --- | --- | --- | --- |
| Gespeicherte Wölbung, identische Vorgabe: Düse 0,4 mm, **Breite 0,42 mm**, vier Lagen à 0,2 mm, Flow 0,98 | Zufuhr/Schale 0,997321989773; äußere Projektionslücke 0,2607926873 %, Überstand 0,1032010617 %. | Zufuhr/Schale 0,997321989833; dieselben Projektionsanteile. Teilungsrest weiterhin 3,6379788071 × 10⁻¹² mm³. Räumlicher Audit: `ResourceLimit`, Sweep-Flächenbudget überschritten. | Keine behauptete Verbesserung der gekrümmten Materialabdeckung; räumliche Fehl-/Übermengen unbekannt. [Werte 05](formorix/verification/auftrag-05/package5-tests.txt), `native_curved_wall_standard04`. |
| Dieselbe Markierung, analytisches 0,8-/0,88-mm-Profil | Zufuhr/Schale 0,975396341311; Projektionslücke 2,4193280 %. | 0,975396341370; räumlicher Audit `InvalidSweep`. | Eigenständiges Profil, kein Ersatz für den 0,42-mm-Vergleich. |
| Hydrofoil-Negativfläche, bisheriger innerer Ausschnitt | Zufuhr/Schale 0,831208642920; Projektionslücke 16,2231432 %. | 0,831208643046; räumlicher Audit `InvalidSweep`. | Weiterhin unvollständig. Neue vollständige Hydrofoil-Tests stehen separat unten. |
| Kommensurable Ebene und zwei angrenzende Materialregionen | Positive ebene Abdeckungs-/Budgettests; Zufuhrfaktoren nahe 1. | Zusätzlich exakter räumlicher Nachweis aller Lagen, einschließlich innerer Fehlmengen, Mehrfachbelegung und Materialgrenzen. | Drei bestehende regionale Referenzen mit stärkerer räumlicher Evidenz. |
| Scharfe planare Gehrung mit umklappender innerer Feinreihe | Ablehnung. | Gültige komplementäre Innentriangulierung für geprüfte konstante planare Schalen; Volumenrest 0. | Geometrische Erweiterung, keine allgemeine geneigte Materialfreigabe. |
| Auflage der gespeicherten Wölbung | 6.006 fehlende RAW-Proben von 22.400. | Historische RAW-Kohorte: 6.006 → 5.998 nach 25 nativen Verdichtungsmodifiern. Neue Schnittstellenkohorte separat: 10.135 → 10.130 von 36.496; 7.302 native Schichtlücken bleiben. | Kleine geometrische Verbesserung, keine Tragfähigkeitsfreigabe. [Auflage 04](formorix/verification/auftrag-04-nachtrag/core-tests.txt), [Auflage 05](formorix/verification/auftrag-05/package5-core.txt). |
| Drei angrenzende Materialregionen | Positiver ebener Zweimaterialfall; kein entsprechender räumlicher Dreimaterialnachweis. | Düsen 0,8/0,4/0,6 mm, A/B/C: 12/9/18 mm³; zusammen 39 mm³ Schale und 321 mm³ Kern. Gemeinsame Grenzen ohne nominale Fehlmenge oder Überlagerung. | Drei regionale Nachweise mit unterschiedlichen Lagenhöhen und Schalentiefen; Haftung und Parallelplanung ungeprüft. |
| Nominale räumliche Fehl-/Übermengen | Projektionsdiagnosen und Mengenverhältnisse, kein allgemeiner räumlicher Materialaudit aller Lagen. | Positive räumliche Referenzen: jeweils 0 mm³ Fehl-/Außenmaterial, Mehrfachbelegung und Kerneingriff. Gegenproben erkennen 0,24 mm³ Randdefizit und 3 mm³ fehlende Innenlage. | Bei fehlgeschlagenem Audit bleiben Mengen unbekannt; insbesondere keine Nullfehlerbehauptung für die gespeicherte Wölbung. |
| Positive/nicht positive Fälle der festen Kohorte | 3 positiv / 14 nicht positiv. | Unverändert 3 positiv / 14 nicht positiv. | Bestehende positive Fälle erhalten stärkere räumliche Evidenz; keine verbesserte Erfolgszahl dieser Kohorte. |
| Erweiterter positiver Referenzumfang | Drei regionale Referenzen der historischen Kohorte. | Insgesamt neun unterschiedliche regionale Nachweise, aufgeschlüsselt unten. | Neue Geometrien getrennt gezählt; sechs Eingabereihenfolgen und erneuter 3MF-Roundtrip erhöhen die Anzahl nicht. Keine Ganzbauteilfreigabe. |

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

Die vollständige finale CTest-Auswahl wurde **ohne Ausschlüsse** ausgeführt. Gezielte Paketläufe überlappen sich und werden nicht zu einer Gesamttestzahl addiert.

| Prüfung | Ausgeführtes Ergebnis und Protokoll |
| --- | --- |
| Finale vollständige CTest-Regression | **3.284 ausgewählt, 3.278 bestanden, 6 Windows-Symlink-Skips, 0 Fehler; 296,69 s.** Keine ausgeschlossenen Tests. [Abschlussprotokoll](formorix/verification/auftrag-05/ctest-final.txt). |
| Finales clang-tidy | **54 geprüfte Dateien bestanden.** [Abschlussprotokoll](formorix/verification/auftrag-05/clang-tidy-final.txt). |
| gettext und deutsche Kataloge | **460 neue deutsche Übersetzungen; 685 Formorix-Meldungen erfasst.** Die 66 bereits vorhandenen Übersetzungs-Prüfwarnungen bleiben unverändert; keine neuen Warnungen. [gettext](formorix/verification/auftrag-05/package9-gettext.txt), [Katalogprüfung](formorix/verification/auftrag-05/package9-po-check.txt), [Prüfumfang](formorix/verification/auftrag-05/package9-localization.txt). |
| Veröffentlichte Hilfe | **13 Themen, 5 Anker, 32 relative Links und 22 HTTP-Ziele mit Status 200** geprüft. [Linkprotokoll](formorix/verification/auftrag-05/package9-help-links.json). |
| Native Projektportabilität und Profilkonflikte | **19 Fälle / 1.149 Assertions bestanden.** Neun native Presets, A/B/C-IDs und frische Profilauflösung erhalten; echte gleichnamige Profilkonflikte werden abgewiesen. [Roundtrip-Nachweise und Grenzen](formorix/verification/auftrag-05/project-roundtrip.md), [Abschlusslauf](formorix/verification/auftrag-05/package9-portability-final.txt). |
| Breite Formorix-Regression nach nativer Unterbauintegration | 155 Fälle / 433.708 Assertions; native Orca-/Ausgabesperren zusätzlich 32 Fälle / 31.634 Assertions. [Geometrie/Modelle](formorix/verification/auftrag-05/package5-tests.txt), [native Integration](formorix/verification/auftrag-05/package5-core.txt). |
| Materialvorschau und Posen | 42 Fälle / 86.355 Assertions. [Protokoll](formorix/verification/auftrag-05/package6-tests.txt). |
| Verschärfte Genauigkeitsgates | 35 Fälle / 189.901 Assertions. [Protokoll](formorix/verification/auftrag-05/package7-tests.txt). |
| Zusätzliche Pflichtgeometrien und große Koordinaten | 3 geometrische Fälle / 111 Assertions; 1 nativer Fall / 689 Assertions. [Geometrie](formorix/verification/auftrag-05/package8-tests.txt), [Orca bei 1600 mm](formorix/verification/auftrag-05/package8-core.txt). |
| Reihenfolgeunabhängigkeit | Material-/Referenzfälle mit Seeds 7/1337 jeweils 22 Fälle / 855 Assertions. [Seed 7](formorix/verification/auftrag-05/package1-order-7.txt), [Seed 1337](formorix/verification/auftrag-05/package1-order-1337.txt). |

Die neueren gezielten Läufe verwenden Seed 42. Tests decken innere Lücken, Limits, Abbruch, leere/ungültige Ablage, verlorene Topologie, Materialidentität, unbekannte CAD-Referenzen und ungültige/veraltete Profile ab. Automatische Vollwandauflage, Kinematik, Kollisionsfreigabe, Mehrkopf-Scheduler, Klipper oder ausführbare 5D-/Hybriddateien wurden nicht implementiert. Die experimentellen Export-, Cache- und Sendesperren bleiben erhalten.

## Performancevergleich

Die Auftrag-04-Basis und der finale Auftrag-05-Stand wurden mit derselben Basistest-Auswahl und Seed 42 gemessen. Alle drei Endläufe hatten Exitcode 0, ohne gleichzeitig laufenden Build oder Test. Es handelt sich jeweils um eine Einzelmessung einschließlich Prozessstart und Assertions; Speicher ist der alle 50 ms abgefragte Peak Working Set, keine isolierte Kernelmessung.

| Basistest-Auswahl | Auftrag 04 Laufzeit | Auftrag 05 Laufzeit | Peak Working Set 04 | Peak Working Set 05 |
| --- | ---: | ---: | ---: | ---: |
| Gespeicherte Wölbung, beide getrennten Testprofile | 8,3289462 s | 8,9043811 s | 77.426.688 Byte | 128.503.808 Byte |
| Geneigte/konvexe/konkave/Hydrofoil-Ausschnitte | 0,253531 s | 0,2418938 s | 25.038.848 Byte | 26.341.376 Byte |
| Kommensurable Ebene | 0,1205067 s | 0,2317757 s | 15.228.928 Byte | 26.574.848 Byte |

[Basismessung 04](formorix/verification/auftrag-05/baseline-performance.json), [Endmessung 05](formorix/verification/auftrag-05/final-performance.json). Die 05-Fälle enthalten zusätzliche räumliche Materialprüfungen. Die Messung belegt weder eine allgemeine Beschleunigung noch eine vollständig unterstützte gekrümmte Materialablage; bestandene erwartete Audit-Ablehnungen sind eingeschlossen. Ein früher Diagnoseprozess wurde nach über acht CPU-Minuten beendet und nicht als bestanden gewertet. Das Materialaudit besitzt nun ein kooperatives 60-Sekunden-Budget; einzelne nicht unterbrechbare CGAL-Operationen können es überschreiten.

## Stand der Release- und GUI-Abnahme

Release-Build, Installation und die [dokumentierte sichtbare Endabnahme](formorix/verification/auftrag-05/gui-verification.md) sind abgeschlossen. Der GUI-Bericht trennt tatsächlich bediente Abläufe, automatisierte Prüfungen und weiterhin ungeprüfte Fälle.

| Endprüfung | Abschlussstatus |
| --- | --- |
| Finaler Quellcommit und Windows-Release-Build | Sauberer Stand `b066c16391dbee033abbd8a03f7379c6ff9394ac`; `ALL_BUILD` Exitcode 0. Generierter Versionsheader: `b066c16391`, leerer Zusatz. [Buildprotokoll](formorix/verification/auftrag-05/build-release-final.txt). |
| Installationsartefakt mit SHA256 | Installation Exitcode 0 nach `build/Formorix-Auftrag05`. SHA256 für EXE, Anwendungs-DLL und deutschen MO-Katalog sind im [Artefaktmanifest](formorix/verification/auftrag-05/release-manifest.json) festgehalten. |
| Portables Windows-Archiv | `Formorix-Slicer-Auftrag05-Windows-b066c16391.zip`, 221.535.321 Byte; 18.799 Dateien vollständig gegen die Installation per Hash geprüft. SHA256 `62ecb91cd67bdc22381d24ea303dcd90e30cc6e680820d79ff1176b3041605b6`. [ZIP-Manifest und lokaler Pfad](formorix/verification/auftrag-05/portable-zip.json). |
| Native GUI: Materialanzeigen, drei Kopfzustände, Zeitleiste und veraltete Ergebnisse | Drei Kopfmarkierungen/-zustände, Darstellungswechsel, ±1 s, Schieberegler, 4×-Wiedergabe, Pause und Reset bedient. Während einer Berechnung geänderte Schichtzahl führte zum Verwerfen des alten Ergebnisses. Zwei reale Hilfe-Aufrufe bestanden. [GUI-Bericht mit zehn Screenshots](formorix/verification/auftrag-05/gui-verification.md). |
| Projekt-/GUI-Roundtrip und Rückkehr zum normalen Orca-Slicing | Nativ gespeichert, neu geladen und berechnet: 39 mm³ Materialunion, regionale Volumen 12/9/18 mm³, keine Fehlmenge/Überstände/Kerneingriffe; CAD unbekannt. Feste Breite erzeugte sichtbar zwei rote Fehlstreifen mit 2,25395327 mm³ Fehlmenge. Hybridexport und Senden blieben gesperrt. Konventioneller Würfel wurde tatsächlich exportiert; ausführbare Befehle stimmen mit der Basis überein. [Archivvergleich](formorix/verification/auftrag-05/gui-project-archive.json), [G-Code-Vergleich](formorix/verification/auftrag-05/gui-cube-export.json). |
| Offen in der nativen GUI-Abnahme | Ausdrücklicher Abbruchknopf und nichtleere Überstand-/Überlagerungsanzeigen wurden nicht nativ geprüft; hierfür liegen automatisierte Tests vor. Die geprüfte Änderung während einer Berechnung belegt Ergebnisinvalidierung, keinen ausdrücklich bedienten Abbruch. |
| Performancevergleich 04/05 | Drei abgeschlossene Einzelmessungen, alle Exitcode 0; Laufzeit und Speicherverbrauch samt methodischen Grenzen oben dokumentiert. |

Der erfolgreiche Release-Build enthält weiterhin `LNK4098`-Warnungen zum Konflikt der Standardbibliothek `LIBCMT` bei `orca_stubgen`, `OrcaSlicer` und `slic3rutils_tests`. Sie sind im Buildprotokoll erhalten; bestandene Tests und GUI-Prüfungen beseitigen diese Warnungen nicht.

Andere Betriebssysteme und reale Druckhardware sind nicht abgenommen. Der Stand ist ein besser überprüfbarer experimenteller Prototyp; die verbleibenden Grenzen der gekrümmten Materialabdeckung erlauben keine Behauptung, Auftrag 05 sei uneingeschränkt erfüllt.
