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

Die weiteren Arbeitspakete, native GUI-Abnahme, vollständige CTest-Regression, gettext, clang-tidy, Projekt-Roundtrip und Exportregression sind noch nicht abgeschlossen und werden erst nach tatsächlicher Durchführung eingetragen.
