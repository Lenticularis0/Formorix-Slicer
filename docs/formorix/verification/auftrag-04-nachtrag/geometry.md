# Geometrischer Prüfbericht zum Nachtrag von Auftrag 04

Prüfdatum: 11. November 2026. Der finale gezielte Windows-Release-Lauf mit den Tags
`[FormorixFoundations],[FormorixHybrid],[FormorixPreview],[RibbonModel]` und RNG-Seed 42
bestand mit **57 Testfällen und 221.257 Assertions**. Das unveränderte
[Rohprotokoll](geometry-tests-final.txt) enthält alle unten ausgewerteten Messzeilen.
Der zugehörige Implementierungsstand ist `2d5db079508df40f68a20de83942221bb8a67524`.
Die vollständige Windows-Regression und GUI-Abnahme werden getrennt im
[Abschlussbericht](../../../Formorix-Auftrag-04-Nachtrag.md) dokumentiert.

Das Ergebnis ist ein erheblich besser bilanziertes, weiterhin experimentelles
Nominalmodell. **Eine allgemeine geometrische Gesamtabweichung von höchstens
0,01 mm ist nicht nachgewiesen.** Ein positiver Nachweis besteht für die unten
definierten kommensurablen ebenen Referenzen. Die echte gespeicherte Wölbung ist
berechenbar, enthält aber noch fehlende Abdeckung und nominalen Überstand. Sie
erhält deshalb keine Genauigkeitsbestätigung. Reale Materialform, Druckgenauigkeit,
Haftung, Tragfähigkeit und Kollisionsfreiheit sind nicht Bestandteil dieses Nachweises.

## Modell und überprüfte Bilanz

`CoalescedOrcaStadiumRibbon/v1` übernimmt Orcas nominalen Querschnitt und den
unveränderten Profil-Flow `f`:

`A₀ = h(w − h) + πh²/4`, `A = A₀ f`, `p = A/h`.

Die angenommene kompaktierte Zelle besitzt normale Höhe `h` und Breite `p`.
Profil-Linienbreite `w` und abgeleitete Zellbreite `p` sind unterschiedliche Größen.
Es gibt keine automatische Flow-Korrektur, keine Abstandsverkleinerung zur
Schönung der Kontur und keine globale Skalierung auf ein gewünschtes Volumenverhältnis.
Die Verschmelzung zu solchen Zellen ist ausdrücklich eine unkalibrierte Annahme.
Die [mathematische Definition](deposition-model.md) erläutert auch, warum starre
Ellipsen mit sehr kleinen Abständen den früheren Materialüberschuss verursachten.

Endliche Rasterzüge werden als zusammenhängender Gehrungs-Sweep mit flachen
Endkappen ausgewertet. Umkehrpunkte liegen eine halbe Zellbreite vor dem Rand.
Für ein ebenes Rechteck `W × D` mit `D = Np` beträgt die gesamte Mittellinienlänge
einschließlich Verbinder `L = NW`. Damit ist `AL = WDh`. Diese Aussage wird mit
der tatsächlichen endlichen Polygonvereinigung überprüft; unabhängige
Segmentrechtecke wären an den Umkehrstellen falsch.

Ein nicht zur Zellbreite passender Reststreifen bleibt fehlend. Örtlich nicht in
die zugeteilte Schale passende Abschnitte werden zu Bewegungen ohne Extrusion.
Die Eignungsprüfung tastet den Querschnitt und die tatsächliche Schale lokal ab;
sie ist kein vollständiger räumlicher Sweep- oder Kollisionsbeweis. Insbesondere
zeigt die Wölbung unten weiterhin einen messbaren Projektionsüberstand.

Das Materialvolumen eines extrudierten Segments ist `A × Materialmittellinienlänge`.
In der Zeitleiste wird diese Menge auf die tatsächliche TCP-Segmentlänge umgerechnet.
Tests prüfen die Übereinstimmung des Zeitleistenintegrals mit dem Regionsledger,
auch bei gekrümmter Oberfläche und 7 mm Kontaktpunkt-/TCP-Versatz. Bewegungen ohne
Extrusion haben `mm3_per_mm = 0`; NaN, negative oder unendliche Dichten und
Materialzufuhr bei einer solchen Bewegung werden zurückgewiesen.

Jede positive Übermenge über `10⁻⁸ max(1, regionales Schalenvolumen)` mm³ wird als
unzulässig gekennzeichnet. Der globale Boolean-Volumenrest vergrößert diese
regionale Toleranz nicht. Mehr als 110 % des regionalen Zielvolumens zuzüglich
derselben numerischen Toleranz verwirft den Plan. Die 110-%-Grenze ist keine
Erlaubnis kleinerer Überschüsse. Unterfüllung bleibt durch Abdeckungsprüfung und
Volumenbericht sichtbar; eine Bilanz nahe 1 allein ist kein Genauigkeitsbeweis.

## Tatsächliche Hybridmetriken

`V-Bahn / V-Schale` ist die nominale Zufuhr über alle Lagen, geteilt durch das
zugeteilte geometrische Schalenvolumen. Die beiden Flächenanteile beziehen sich
auf die Projektion der **äußersten** Lage: fehlende Fläche beziehungsweise Fläche
außerhalb der markierten Zielprojektion, jeweils geteilt durch die Zielprojektionsfläche.
Bei gekrümmten Bereichen sind dies Diagnosewerte, keine vollständige räumliche
Abdeckung oder Volumenzerlegung. Der Genauigkeitstest berücksichtigt dagegen
Abdeckung und Mengenbilanz **jeder** Lage.

`∞ / ∞ / ∞` bedeutet, dass mindestens eine erforderliche Hüllenprobe keinen
Materialtreffer besitzt. Maximal-, Mittel- und RMS-Wert über die gesamte
Prüffläche werden dann ausdrücklich nicht als endliche Fehlerwerte ausgegeben.
Eine Statistik nur über die erfolgreichen Treffer würde die Lücken verschweigen.

| Fall / Region | Max. / Mittel / RMS, mm | Fehlende Projektionsfläche, % | Projektionsüberstand, % | V-Bahn / V-Schale | Nominaler 0,01-mm-Test |
| --- | ---: | ---: | ---: | ---: | --- |
| Kommensurable Ebene, `p = 0,75 mm` | 0 / 0 / 0 | 0 | 0 | 0,999999999069 | bestanden |
| Kommensurable Materialgrenze, Kopf 1 | 0 / 0 / 0 | 0 | 0 | 0,999999999069 | bestanden |
| Kommensurable Materialgrenze, Kopf 2 | 0 / 0 / 0 | 0 | 0 | 1,000000000621 | bestanden |
| Ebene mit verbleibendem Randstreifen | ∞ / ∞ / ∞ | 16,2920333 | 0 | 0,837079631900 | nicht bestätigt |
| Benachbarte Bereiche mit Standardbreiten, Kopf 1 | ∞ / ∞ / ∞ | 16,2920333 | 0 | 0,837079631900 | nicht bestätigt |
| Benachbarte Bereiche mit Standardbreiten, Kopf 2 | ∞ / ∞ / ∞ | 7,3480667 | 0 | 0,926519143494 | nicht bestätigt |
| Geneigte Fläche / variable Wandstärke | ∞ / ∞ / ∞ | 16,2920333 | 0,0000180464 | 0,837079632679 | nicht bestätigt |
| Konvexe Wölbung, innerer Ausschnitt | ∞ / ∞ / ∞ | 16,4705631 | 0,6719210 | 0,756808776351 | nicht bestätigt |
| Konkave Wölbung, innerer Ausschnitt | ∞ / ∞ / ∞ | 16,4705316 | 0,6719036 | 0,737579390096 | nicht bestätigt |
| Hydrofoil-Negativfläche, innerer Ausschnitt | ∞ / ∞ / ∞ | 16,2231432 | 0,0181264 | 0,831208642920 | nicht bestätigt |
| Vollständig markierte geneigte Oberseite | ∞ / ∞ / ∞ | 8,1177354 | 0 | 0,925745631532 | nicht bestätigt |
| Vollständig markierte konvexe Oberseite | ∞ / ∞ / ∞ | 3,5198611 | 3,4123615 | 0,794246097200 | nicht bestätigt |
| Vollständig markierte konkave Oberseite | ∞ / ∞ / ∞ | 27,5486509 | 0,0192236 | 0,583659682788 | nicht bestätigt |
| Vollständig markierte Hydrofoil-Negativoberseite | ∞ / ∞ / ∞ | 27,1285900 | 0 | 0,731363035197 | nicht bestätigt |
| Gespeicherte Wölbung, Düse 0,8 / Breite 0,88 mm | ∞ / ∞ / ∞ | 2,4193280 | 0,2250537 | 0,975396341311 | nicht bestätigt |
| Gespeicherte Wölbung, Düse 0,4 / Breite 0,42 mm | ∞ / ∞ / ∞ | 0,2607927 | 0,1032011 | 0,997321989773 | nicht bestätigt |
| Konvexer Ausschnitt mit 7 mm TCP-Versatz | ∞ / ∞ / ∞ | 16,4705631 | 0,6719210 | 0,756808776351 | nicht bestätigt |

Die gespeicherte Standardwölbung verwendet vier Lagen mit `h = 0,2 mm`,
Flow `0,98` und 60 mm/s als Profilgeschwindigkeit. Daraus folgt
`p ≈ 0,369538040 mm`. Ihr früherer Zufuhrfaktor **4,642359** wird zu
**0,997321989773**, ohne den Flow zu verändern. Die verbleibenden Lücken und
Überstände verhindern dennoch eine vollständige Freigabe.

Die positiven ebenen Referenzen sind gezielt kommensurabel: Die erste hat einen
3 × 3 mm großen markierten Bereich und vier Zeilen pro Lage mit `p = 0,75 mm`.
Der Zweikopftest verwendet benachbarte 1,5 × 3 mm große Bereiche mit Düsen
0,8 / 0,4 mm, unterschiedlichen Material-IDs, `p = 0,75 / 0,375 mm`,
`h = 0,2 mm` und zwei beziehungsweise drei Lagen. Ihre gemeinsame Grenze liegt
identisch bei `x = 0`; die endlichen Sweeps haben innerhalb der geprüften
numerischen Toleranz weder Lücken noch Überstand noch doppelt bilanziertes
Material. Das beweist keine Haftung zwischen den beiden Materialien.

## Geometrische Volumina und Konvertierung

Das Residuum ist `V-Kern + ΣV-Schale − V-Original` nach Double-Ausgabe. Die
Konvertierungsspalte ist die im Hybridmodul gemessene Positionsschranke für dessen
Float-Übergabe. Eine zusätzliche Konvertierung des Kerns in den nativen
Orca-Adapter wird separat im Gesamtbudget erfasst. Ein Wert null besagt nicht,
dass sämtliche Orca-Algorithmen oder die importierte CAD-Oberfläche fehlerfrei sind.

| Hybridfall | Volumenresiduum, mm³ | Float-Positionsschranke im Hybridmodul, mm |
| --- | ---: | ---: |
| Ebene, beide ebenen Mehrmaterialfälle | 0 | 0 |
| Geneigter innerer Ausschnitt | 0 | 0 |
| Konvexer innerer Ausschnitt / TCP-Versatz | 2,9934881240e-9 | 4,2886241403e-7 |
| Konkaver innerer Ausschnitt | −2,9934881240e-9 | 1,0662926409e-7 |
| Hydrofoil-Negativfläche, innerer Ausschnitt | −4,4104808694e-10 | 2,1055788846e-7 |
| Vollständige geneigte Oberseite | 2,8421709430e-14 | 0 |
| Vollständige konvexe Oberseite | 2,9935449675e-9 | 4,2886241403e-7 |
| Vollständige konkave Oberseite | −2,9935023349e-9 | 1,0662926409e-7 |
| Vollständige Hydrofoil-Negativoberseite | −4,4107650865e-10 | 2,1055788846e-7 |
| Gespeicherte Wölbung, beide Einzelregion-Profile | 3,6379788071e-12 | 0 |

Die unabhängige Geometrieprüfung der **beiden** gespeicherten Markierungen von
`curved-wall.3mf` besteht ebenfalls: Eingangsvolumen 18211,860008239746 mm³,
Volumenresiduum 0 mm³, 20 gekoppelte gemeinsame Randkanten und
CGAL-/Ausgaberundungsgrenze 6,0011491504e-10 mm. Die
[Gehrungsprüfung](geometry-miter.md) beschreibt die eigenen Bereichsnormalen,
ungleichen Dicken und scharfen Grenzflächen. Dieses Zweiregionenprojekt liefert
wegen der unten beschriebenen Düsenorientierung trotzdem keinen akzeptierten
Hybrid-Druckplan.

Der Fullwall-Test prüft eine 4 × 4 × 1 mm große Platte: 16 mm³ geometrische
Schale, leerer konventioneller Kern, fünf Lagen und fehlende Auflage ohne
vorausgehende Struktur. **16 mm³ ist hier das Schalenvolumen, nicht ein Nachweis
von 16 mm³ tatsächlich geplanter Ribbon-Zufuhr.** Die gewählte Standardbreite
ist nicht kommensurabel; der Test behauptet keine vollständige Materialabdeckung.

## Analytische Grundlagenreferenzen

Die folgenden im selben Lauf wiederholten Grundlagenmessungen prüfen jeweils die
genannte Geometrieoperation. Sie sind von den oben berichteten vollständigen
Hybridfällen getrennt. Insbesondere ein genauer Kontaktbogen oder eine genaue
Importkurve beweist keine Abdeckung einer ganzen 5D-Fläche.

| Operation / Referenz | Maximal, mm | Mittel, mm | RMS, mm | Geprüfte obere Schranke, mm |
| --- | ---: | ---: | ---: | ---: |
| Ebene Grenzfläche | 0 | 0 | 0 | 0 |
| Konvexer bzw. konkaver feiner Import | 0,000234371948 | 0,000169267047 | 0,000173715933 | 0,000471720058 |
| Konvexer bzw. konkaver grober Import | 0,236817017991 | 0,156585145942 | 0,171921370051 | 0,237061424061 |
| Umlaufende Referenz, Import | 0,003613631384 | 0,002421337115 | 0,002638950569 | 0,003842429568 |
| Umlaufende Referenz, innere Grenzfläche | 0,003312495545 | 0,002219558981 | 0,002419037983 | 0,003522227212 |
| Kugel, zwei Krümmungsrichtungen | 0,000211172391 | 0,000143024490 | 0,000144925967 | 0,000424308171 |
| Kleiner Radius, Import | 0,000016275600 | 0,000016275600 | 0,000016275600 | 0,000075598587 |
| Analytische NACA0012-CAD-Polylinie | 0,000382542361 | 0,000070948042 | 0,000102070116 | 0,000991239999 |
| Konvexer kompensierter Kontaktbogen | 0,000193358746 | 0,000122460513 | 0,000137805677 | 0,000193359375 |
| Konkaver kompensierter Kontaktbogen | 0,000197264983 | 0,000124934462 | 0,000140589630 | 0,000197265625 |

Der grobe Import verfehlt 0,01 mm eindeutig und wird nicht durch feinere
Bahninterpolation geheilt. Die zusätzliche Grundlagenprüfung des kleinen Radius
meldet für das dort getestete starre Materialmodell eine Grenze von
0,075610283454 mm. Die weiterhin protokollierten `planar_bead`, `concave_bead`,
`convex_bead` und `COVERAGE head_*`-Zeilen betreffen das unveränderte ältere
Querschnitts-Grundlagenmodell; ihre Einzelkontaktfehler und Überlappungsdiagnosen
sind **keine** Genauigkeitswerte des neuen Ribbon-Hybridplaners.

## Fehlerbudget und Qualitätseinstellung

Die fünf Fehleranteile bleiben getrennt: CAD-/Mesh-Import, Schalenberechnung,
Float-/Orca-Konvertierung, Bahninterpolation und nominale Materialablage.
Für die positive Prüfung müssen ihre endlichen Schranken zusammen höchstens
0,01 mm ergeben, die endliche Abdeckung jeder Lage bestehen und deren
Zufuhr/Flächenvereinigung/zugeteiltes Volumen innerhalb der deklarierten Numerik
übereinstimmen. Die 10-%-Plausibilitätsdiagnose ist ausdrücklich kein Ersatz dafür.

Eine relative Flächenlücke beweist keinen maximalen Abstand. Die positive ebene
Prüfung verwendet deshalb beidseitige Mengeninklusion mit explizitem 0,000002-mm-
Puffer (**2 nm, nicht 2 µm**). Der Materialfehler berücksichtigt zusätzlich das
tatsächliche Integer-Raster, die Float-Konvertierung des Offsetradius, die
Gehrungsverstärkung und die absolute Abweichung von der Referenzebene. Als
Gesamtwert zählt der größte Materialbound aller Lagen. Die Ebenenerkennung prüft
zusätzlich zu den Normalen die absolute Höhe sämtlicher Dreiecksvertices; bei
linearen Dreiecken begrenzt dies auch die dazwischenliegende Fläche.

Bei unbekannter CAD-Referenz, nicht begrenzter räumlicher Hülle oder gekrümmter
Gehrungsgrenze bleiben die betreffenden Bounds unendlich. Eine unveränderte
Originalkontur und ein kleiner Boolean-Volumenrest ersetzen diese Nachweise nicht.
Der Regressionstest mit einer vollständig bedeckten feinen Außenlage und
fehlenden inneren Ribbon-Zellen bestätigt ausdrücklich, dass dieser Fall keine
positive Genauigkeitsprüfung erhält.

Die vorhandene Qualitätswahl verändert nur Längs- und Normalenauflösung:

| Qualität | Max. Längsschritt, mm | Mittelpunkt-Abweichung, mm | Max. Änderung der Einheitsachse |
| --- | ---: | ---: | ---: |
| 0 / Entwurf | 1,0 | 0,002 | 0,040 |
| 1 / Standard | 0,5 | 0,0005 | 0,015 |
| 2 / Fein | 0,2 | 0,0001 | 0,005 |

Im ausgeführten gekrümmten Vergleich entstehen **213 gegenüber 873 Punkten**;
`p = 0,83707963267948993 mm`, Querschnitt, Profilbreite und Flow bleiben identisch.
Beide Ergebnisse bleiben geometrisch unbestätigt. Auch der ursprüngliche
Oberflächen-Vorschaumodus wird auf die Wirkung der Qualitätswahl geprüft.

## Unterstützungsumfang und bewusste Zurückweisungen

| Fall | Tatsächlicher Umfang |
| --- | --- |
| Kommensurable analytische Ebene und zwei unterschiedliche Düsen an gemeinsamer ebener Grenze | Vollständige nominale Abdeckung und positiver 0,01-mm-Fehlerbudgettest für diese Referenzen. |
| Geneigte, konvexe, konkave, variable und Hydrofoil-Negativflächen | Gültige Kern-/Schalenzerlegung und Diagnosebahnen; Reststreifen, lokale Terminierung und räumliche Abdeckung verhindern eine allgemeine Freigabe. |
| Echte gespeicherte Wölbung mit Standardprofil | Vorschau und Zeitleiste berechenbar; Faktor nahe 1, verbleibende Abdeckungslücken/Überstände ausdrücklich unbestätigt. |
| Zwei gespeicherte Regionen einschließlich senkrechter Wand | Geometrische Gehrungszerlegung besteht; die benötigte starre geneigte Düsenform wird vom Materialplaner abgewiesen. |
| Bis 15° Abweichung von der Normalen | Keine angenommene Material-Scherung. Passt die starre Querschnittsform nicht zur Normalhöhe, erfolgt eine klare Ablehnung. Eine Oberfläche mit tatsächlich normal ausgerichteter Düse bleibt möglich. |
| Feine Außenlage über unvollständiger Innenlage | Berechenbare Diagnose; positive Gesamtabdeckung wird verweigert. |
| Fullwall ohne Kern | Schalenvolumen und fünf Lagen werden geprüft; notwendiger vorausgehender Unterbau bleibt fehlend. |
| Gefaltete Offsets, überholte innere Dreiecke, inkonsistente oder zu große Gehrung | Ausdrückliche Zurückweisung; keine versteckte Priorität zwischen Materialien. |
| Doppelte Flächenzuordnung, unpassende Modell-/Prozessmodifikatoren, ungültige oder nichtendliche Profile | Fehler statt stillschweigend veränderter Modellsemantik. |
| Mehr als 110 % nominale Zufuhr oder erschöpftes Bewegungsbudget | Abbruch ohne akzeptierte Teilvorschau; kein automatisches Herunterskalieren des Flows. |

Die tatsächlichen Orca-Kernbahnen werden im separaten Integrationstest geprüft.
Dieser besteht mit **18 Fällen / 5.237 Assertions**. Im gespeicherten Wölbungsfall
fehlen 6.006 von 22.400 nominalen Auflagekontakten, sämtlich als Bahnkontaktlücke
bei vorhandenem Kern; 54 besitzen keinen geeigneten Kontakt innerhalb von 0,5 mm.
Die größte gefundene endliche Lücke beträgt 0,494771 mm. Diese Suchweite ist nur
diagnostisch; sie erweitert nicht die erlaubte Auflagedistanz. Die Kategorien,
Stadiumquerschnitte und GUI-Farben erläutert der
[Abschlussbericht](../../../Formorix-Auftrag-04-Nachtrag.md#auflage-auf-tatsächlichen-orca-bahnen).

Die Tests liegen in [test_formorix_hybrid.cpp](../../../../tests/libslic3r/test_formorix_hybrid.cpp),
[test_formorix_foundations.cpp](../../../../tests/libslic3r/test_formorix_foundations.cpp)
und [test_formorix_hybrid_core.cpp](../../../../tests/fff_print/test_formorix_hybrid_core.cpp).
Die übrigen Assertions prüfen Validität, Fehlerzustände, Volumenledger,
Kontinuitäts- und Maskenzuordnung, Profiltrennung und Grenzen; dafür werden keine
erfundenen Abstandsmesswerte angegeben. Die experimentelle Drucksperre bleibt
bestehen. Es gibt keine neue Mehrkopfplanung, Kollisionsfreigabe oder ausführbare
Hybrid-Datei.
